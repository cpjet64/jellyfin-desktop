// Tests the production Quick helper with the production QtGui swap bridge.
// Fake identities and a fake QSGTexture do not establish native-picture proof.
#include "QtRecoveryQuick_p.h"
#include <QtQuick/QSGTexture>
#include <cassert>
#include <cstdlib>
#include <limits>

#undef assert
#define assert(x) do { if (!(x)) std::abort(); } while (false)

class Texture final : public QSGTexture {
public:
    QRhiTexture *rhiTexture() const override { return reinterpret_cast<QRhiTexture *>(uintptr_t(17)); }
    qint64 comparisonKey() const override { return 1; }
    QSize textureSize() const override { return QSize(1, 1); }
    bool hasAlphaChannel() const override { return true; }
    bool hasMipmaps() const override { return false; }
};

struct Fixture {
    QtRecoverySwapTicketV1 ticket{sizeof(ticket), QT_RECOVERY_SWAP_ABI_VERSION, 1, 2, 3, 4, 5, 6, 7, 8};
    QtRecoveryFrameV1 frame{sizeof(frame), QT_RECOVERY_SUBMISSION_ABI_VERSION, 9, 10, 11, 12, 13, 14, 1.0, 15, 0};
    Texture texture;
    int node = 0;
    uint64_t life = 18;
    Fixture() { qtRecoveryRegisterNode(&node, life); }
    ~Fixture() { qtRecoveryUnregisterNode(&node, life); qt_recovery_revoke_v1(ticket.window_lifetime); }
    void begin() { assert(qtRecoveryBegin(ticket)); }
    void publish() {
        QtRecoverySwapTicketV1 copied{};
        qtRecoveryFboBegin(frame.actual_render_fbo, life);
        assert(qt_recovery_publish_native_frame_v1(&frame, &copied));
        assert(copied.frame_serial == ticket.frame_serial);
        qtRecoveryFboEnd(16, 19, &texture);
    }
    void draw() { qtRecoveryMarkDraw(life, ticket.render_target, &texture, texture.rhiTexture(), true); }
    void swap(int ok = 1) { qt_recovery_swap_report_v1(ticket.surface, ticket.platform_context, 1, 1, ok); }
    bool take(uint64_t attempt = 9) {
        QtRecoverySubmissionV1 result{};
        result.size = sizeof(result); result.version = QT_RECOVERY_SUBMISSION_ABI_VERSION;
        return qt_recovery_take_submission_v1(ticket.window_lifetime, frame.renderer_generation, attempt, &result);
    }
};

static void successAndReuse()
{
    Fixture f; f.begin(); f.publish(); f.draw(); f.swap(); qtRecoveryFinish(true, false);
    assert(f.take()); assert(!f.take());
    ++f.ticket.frame_serial; f.begin(); f.draw(); f.swap(); qtRecoveryFinish(true, false);
    assert(f.take()); // same native frame under a new Qt ticket
}

static void zeroAttemptReadiness()
{
    Fixture f; f.frame.attempt = 0; f.begin(); f.publish(); f.draw(); f.swap(); qtRecoveryFinish(true, false);
    assert(f.take(0)); assert(!f.take(0));
}

static void swapFailures()
{
    Fixture f; auto bad = f.ticket; bad.frame_serial = 0; assert(!qtRecoveryBegin(bad));
    f.begin(); f.publish(); f.draw(); qtRecoveryFinish(true, false); assert(!f.take());
    f.begin(); f.draw(); f.swap(0); qtRecoveryFinish(true, false); assert(!f.take());
    f.begin(); f.draw(); f.swap(); f.swap(); qtRecoveryFinish(true, false); assert(!f.take());
    f.begin(); f.draw(); qt_recovery_swap_report_v1(f.ticket.surface + 1, f.ticket.platform_context, 1, 1, 1);
    qtRecoveryFinish(true, false); assert(!f.take());
    f.begin(); f.draw(); f.swap(); qtRecoveryFinish(false, false); assert(!f.take());
    f.begin(); f.draw(); f.swap(); qtRecoveryFinish(true, true); assert(!f.take());
}

static void badDraws()
{
    Fixture f; Texture other; f.begin(); f.publish(); f.swap(); qtRecoveryFinish(true, false); assert(!f.take());
    f.begin(); qtRecoveryMarkDraw(f.life, f.ticket.render_target + 1, &f.texture, f.texture.rhiTexture(), true);
    f.swap(); qtRecoveryFinish(true, false); assert(!f.take());
    f.begin(); qtRecoveryMarkDraw(f.life, f.ticket.render_target, &other, other.rhiTexture(), true);
    f.swap(); qtRecoveryFinish(true, false); assert(!f.take());
    f.begin(); qtRecoveryMarkDraw(f.life, f.ticket.render_target, &f.texture, nullptr, true);
    f.swap(); qtRecoveryFinish(true, false); assert(!f.take());
    f.begin(); qtRecoveryMarkDraw(f.life, f.ticket.render_target, &f.texture, f.texture.rhiTexture(), false);
    f.swap(); qtRecoveryFinish(true, false); assert(!f.take());
}

static void revocationAndReuse()
{
    Fixture f; f.begin(); f.publish(); f.draw(); f.swap(); qtRecoveryFinish(true, false);
    assert(!f.take(99)); qt_recovery_revoke_v1(f.ticket.window_lifetime); assert(!f.take());
    f.begin(); f.draw(); f.swap(); qtRecoveryFinish(true, false); assert(!f.take());
    qtRecoveryUnregisterNode(&f.node, f.life); qtRecoveryRegisterNode(&f.node, f.life + 1);
    f.begin(); f.draw(); f.swap(); qtRecoveryFinish(true, false); assert(!f.take());
    qtRecoveryUnregisterNode(&f.node, f.life + 1);
}

static void invalidFboAndResolve()
{
    Fixture f; QtRecoverySwapTicketV1 active{};
    f.begin(); qtRecoveryFboBegin(f.frame.actual_render_fbo, f.life);
    auto wrong = f.frame; ++wrong.actual_render_fbo;
    assert(!qt_recovery_publish_native_frame_v1(&wrong, &active));
    f.draw(); f.swap(); qtRecoveryFinish(true, false); assert(!f.take());
    f.begin(); qtRecoveryFboBegin(f.frame.actual_render_fbo, f.life);
    assert(qt_recovery_publish_native_frame_v1(&f.frame, &active));
    qtRecoveryFboAbort(); // unsupported MSAA/failed or unknown resolve
    f.draw(); f.swap(); qtRecoveryFinish(true, false); assert(!f.take());
}

static void rejectsFrame(Fixture &f, const QtRecoveryFrameV1 &bad)
{
    QtRecoverySwapTicketV1 active{};
    f.begin(); qtRecoveryFboBegin(f.frame.actual_render_fbo, f.life);
    assert(!qt_recovery_publish_native_frame_v1(&bad, &active));
    f.draw(); f.swap(); qtRecoveryFinish(true, false); assert(!f.take());
}

static void invalidNativeFrame()
{
    Fixture f;
    auto bad = f.frame; bad.discontinuity_id = 0; rejectsFrame(f, bad);
    bad = f.frame; bad.media_pts = -1.0; rejectsFrame(f, bad);
    bad.media_pts = -std::numeric_limits<double>::max(); rejectsFrame(f, bad);
    bad.media_pts = std::numeric_limits<double>::quiet_NaN(); rejectsFrame(f, bad);
    bad.media_pts = std::numeric_limits<double>::infinity(); rejectsFrame(f, bad);
    bad.media_pts = double(std::numeric_limits<int64_t>::max() / 2000) + 1.0; rejectsFrame(f, bad);
    const uint64_t over = uint64_t(std::numeric_limits<int64_t>::max()) + 1;
    bad = f.frame; bad.attempt = over; rejectsFrame(f, bad);
    bad = f.frame; bad.renderer_generation = over; rejectsFrame(f, bad);
    bad = f.frame; bad.entry_id = over; rejectsFrame(f, bad);
    bad = f.frame; bad.discontinuity_id = over; rejectsFrame(f, bad);
    bad = f.frame; bad.source_frame_serial = over; rejectsFrame(f, bad);
    bad = f.frame; bad.render_serial = over; rejectsFrame(f, bad);
}

int main()
{
    successAndReuse(); zeroAttemptReadiness(); swapFailures(); badDraws(); revocationAndReuse();
    invalidFboAndResolve();
    invalidNativeFrame();
}
