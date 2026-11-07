#include "graph.h"
#include "graph_session.h"
#include "graph_access.h"
#include "capture_capability.h"
#include "RestartWire.h"
#include "frame_capture.h"
#include "player/PlayerComponent.h"
#include "restore_test_fixture.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QPainter>
#include <QThread>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>

using namespace HealthyRetirement;
using namespace FixtureRestart;
static const char* activeCase = "setup";
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "%s: check failed at line %d: %s\n", activeCase, __LINE__, #value); std::_Exit(3); } } while (false)

class TestCaptureCapability {
  using Bridge = CaptureBridge;
  static constexpr Identity identity{11, 12, 13};
  static FramebufferObservation fbo(std::uint64_t attachment = 7)
  { return {attachment, 160, 90, 0, 0, 0, true, true}; }
  std::shared_ptr<Bridge> bridge_;
  std::optional<Bridge::Ticket> queued_;
public:
  explicit TestCaptureCapability(const std::shared_ptr<Slot>& slot) : bridge_(slot->captureBridge()) {}
  bool initialize() { return bridge_ && bridge_->rendererCreated(identity) && bridge_->framebuffer(identity, fbo()); }
  bool baselineReady(std::uint64_t attempt) const { return bridge_->ready(attempt, Bridge::Phase::Baseline); }
  bool issue(bool publish, int index = 0)
  {
    if (!bridge_->framebufferCurrent(identity, fbo())) return false;
    const auto ticket = bridge_->issue(identity, fixture_restart::nextRenderSerial());
    if (!ticket.request) return false;
    if (publish) bridge_->publish(ticket, identity, image(index), 0, 0, 0, 0);
    return true;
  }
  bool publishBadImage(int mode)
  {
    if (!bridge_->framebufferCurrent(identity, fbo())) return false;
    const auto ticket = bridge_->issue(identity, fixture_restart::nextRenderSerial());
    if (!ticket.request) return false;
    QImage value = mode == 0 ? QImage(160, 89, QImage::Format_RGBA8888) : image(mode == 1 ? 90 : 0);
    if (mode == 2) value = value.flipped(Qt::Vertical);
    bridge_->publish(ticket, identity, value, 0, 0, 0, 0);
    return true;
  }
  bool wrongContextIssue()
  { return bridge_->issue(Identity{11, 99, 13}, fixture_restart::nextRenderSerial()).request != 0; }
  bool wrongEpochIssue()
  { return bridge_->issue(Identity{11, 12, 14}, fixture_restart::nextRenderSerial()).request != 0; }
  bool queue()
  {
    queued_ = bridge_->issue(identity, fixture_restart::nextRenderSerial());
    return queued_->request != 0;
  }
  bool lateDeliveryRejected()
  {
    if (!queued_) return false;
    bridge_->publish(*queued_, identity, image(0), 0, 0, 0, 0);
    return fixture_restart::takeCaptures().empty();
  }
  bool replayRejected()
  {
    const auto ticket = bridge_->issue(identity, fixture_restart::nextRenderSerial());
    if (!ticket.request) return false;
    const auto frame = image(0);
    bridge_->publish(ticket, identity, frame, 0, 0, 0, 0);
    bridge_->publish(ticket, identity, frame, 0, 0, 0, 0);
    return fixture_restart::takeCaptures().size() == 1;
  }
  void replaceFramebuffer() { CHECK(bridge_->framebuffer(identity, fbo(8))); }
  void cancelRenderer() { bridge_->rendererDestroyed(); }
  static void staleRendererGeneration()
  {
    struct Incarnation {
      int context = 0;
      std::shared_ptr<Slot> slot = std::make_shared<Slot>(9, 100, [] { return qint64{1}; });
      std::shared_ptr<RetirementClaim> claim = std::make_shared<RetirementClaim>(9);
      std::unique_ptr<CaptureController> controller;
      std::unique_ptr<TestCaptureCapability> renderer;
      Incarnation() {
        HealthyRetirement::Operations ops;
        ops.currentIdentity = [] { return identity; };
        ops.unregisterCallback = [](void*) {};
        ops.invalidateQueued = [](std::uint64_t) { return true; };
        ops.freeContext = [](void*) {};
        ops.clearPointer = [](void*) { return true; };
        CHECK(slot->beginCreation() && slot->beforeCreate() && slot->recordCreated(&context));
        CHECK(slot->install(identity, &context, ops));
        controller = CaptureController::create(slot); CHECK(controller);
        renderer = std::make_unique<TestCaptureCapability>(slot);
        CHECK(renderer->initialize());
      }
    };
    fixture_restart::disarmCapture();
    (void)fixture_restart::takeCaptures();
    Incarnation old, current;
    const fixture_restart::FrameKey key{1, 2, 3, 4, 5, 0};
    CHECK(old.renderer->bridge_->request(key, 20, Bridge::Phase::Baseline, old.claim));
    CHECK(current.renderer->bridge_->request(key, 20, Bridge::Phase::Baseline, current.claim));
    const auto ticket = current.renderer->bridge_->issue(identity, 1);
    CHECK(ticket.request);
    const auto pixels = image(0);
    current.renderer->bridge_->publish(ticket, identity, pixels, 0, 0, 0, 0);
    CHECK(fixture_restart::takeCaptures().size() == 1);
    const auto stale = old.renderer->bridge_->issue(identity, 2);
    const auto next = current.renderer->bridge_->issue(identity, 2);
    CHECK(stale.request && next.request && stale.renderer != next.renderer);
    CHECK(stale.key == next.key && stale.request == next.request &&
          stale.framebuffer == next.framebuffer && stale.serial == next.serial);
    current.renderer->bridge_->publish(stale, identity, pixels, 0, 0, 0, 0);
    CHECK(fixture_restart::takeCaptures().empty());
    current.renderer->bridge_->publish(next, identity, pixels, 0, 0, 0, 0);
    CHECK(fixture_restart::takeCaptures().size() == 1);
  }
private:
  static QImage image(int index)
  {
    QImage result(160, 90, QImage::Format_RGBA8888); result.fill(Qt::black);
    QPainter painter(&result);
    for (int bit = 0; bit < 9; ++bit) {
      const int value = index & (1 << bit) ? 220 : 70;
      painter.fillRect(5 + bit * 16, 8, 12, 12, QColor(value, value, value));
    }
    painter.fillRect((index * 3) % 140, 35, 20, 20, QColor(220, 220, 220));
    return result;
  }
};
constexpr Identity TestCaptureCapability::identity;

class TestHealthyGraphCapture {
  HealthyRestoreTestFixture fixture_;
  PreparedGraph* prepared_ = nullptr;
  FixtureGraph* graph_ = nullptr;
  std::shared_ptr<RetirementClaim> claim_;
  std::unique_ptr<TestCaptureCapability> renderer_;
  static std::int64_t now()
  {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  static std::unique_ptr<PreparedGraph> prepare()
  {
    const auto start = now();
    return FixtureGraph::prepare(9, start + 10000000000LL, start + 40000000000LL, now);
  }
  static void uninitializedGraphRejectsCapture()
  {
    auto prepared = prepare();
    CHECK(prepared);
    auto* graph = prepared->graphOnGuiThread(); CHECK(graph);
    CHECK(!graph->captureBaseline());
    CHECK(!graph->captureTarget());
    CHECK(!graph->captureTargetReady());
  }
  void observe(quint64 id, const QByteArray& position)
  {
    auto& player = *graph_->player_;
    CHECK(player.requestNativeObservation(player.m_nativeReceipts.snapshot().key, id, 10000, true));
    QByteArray text = "NR1|1|42|yes|yes|" + position + "|yes|no|no";
    mpv_event_command command{}; command.result.format = MPV_FORMAT_STRING; command.result.u.string = text.data();
    mpv_event event{}; event.event_id = MPV_EVENT_COMMAND_REPLY;
    event.reply_userdata = (quint64{1} << 62) | id; event.data = &command;
    player.observeNativeReceipt(&event);
    CHECK(player.m_nativeObservation.valid && player.m_nativeObservation.paused);
  }
  void initialize(bool failClaim = false)
  {
    CHECK(fixture_.initialize());
    prepared_ = fixture_.prepared(); graph_ = fixture_.graph(); CHECK(prepared_ && graph_);
    renderer_ = std::make_unique<TestCaptureCapability>(prepared_->slot());
    if (failClaim) setGraphInstallHookForTest([] { return false; });
    const bool accepted = fixture_.acceptGrant();
    if (failClaim) setGraphInstallHookForTest({});
    claim_ = fixture_.claim();
    if (failClaim) { CHECK(!accepted); return; }
    CHECK(accepted && fixture_.installPermit() && fixture_.load() && fixture_.replyLoad(42));
    CHECK(fixture_.observe(1, 42, "0.000"));
  }
  void armTargetAndSeek()
  {
    CHECK(graph_->captureBaseline());
    CHECK(!graph_->captureTarget());
    CHECK(renderer_->issue(true, 0));
    CHECK(graph_->captureTarget());
    CHECK(!graph_->captureTargetReady());
    CHECK(renderer_->issue(false));
    CHECK(graph_->captureTargetReady());
    CHECK(!graph_->player_->nativeRestoreUnpauseReady());
    auto& player = *graph_->player_;
    CHECK(player.dispatchNativeRestoreSeek());
    mpv_event event{}; event.event_id = MPV_EVENT_COMMAND_REPLY;
    event.reply_userdata = (quint64{1} << 61) | player.m_nativeRestore.key.attempt; player.observeNativeReceipt(&event);
    event = {}; event.event_id = MPV_EVENT_PLAYBACK_RESTART; player.observeNativeReceipt(&event);
  }
  void sampleHold()
  {
    auto& player = *graph_->player_;
    for (quint64 id = 2; id <= 18; ++id) {
      observe(id, "3.000"); CHECK(player.healthyObservePausedHold());
      if (id != 18) QThread::msleep(200);
    }
  }
  void positiveHandoff()
  {
    initialize(); armTargetAndSeek();
    auto& player = *graph_->player_;
    CHECK(renderer_->issue(true, 90)); CHECK(player.healthyCapturePausedHold());
    sampleHold();
    CHECK(player.healthyFinishPausedHold());
    CHECK(player.nativeRestoreUnpauseReady());
    CHECK(!player.nativeRestoreUnpauseComplete());
  }
  void checkNoPlaybackAdvance() const
  {
    CHECK(!graph_->player_->m_nativeRestore.seekAttempted);
    CHECK(!graph_->player_->m_restoreUnpause.attempted);
    CHECK(!graph_->player_->nativeRestoreUnpauseReady());
  }
  void wrongBindingRejected()
  {
    initialize();
    ++graph_->session_->attempt;
    CHECK(!graph_->captureBaseline());
    --graph_->session_->attempt;
    auto other = std::make_shared<RetirementClaim>(9);
    std::atomic_store(&graph_->access_->intentClaim, other);
    CHECK(!graph_->captureBaseline());
    CHECK(!graph_->captureTarget() && !graph_->captureTargetReady());
    checkNoPlaybackAdvance();
  }
  void wrongLaunchBindingRejected()
  {
    initialize();
    CHECK(graph_->restoreBootstrapForTest_);
    auto bootstrap = std::make_shared<FixtureGraph::TestBootstrap>(*graph_->restoreBootstrapForTest_);
    graph_->restoreBootstrapForTest_ = bootstrap;
    auto& launch = bootstrap->binding;
    launch.role = HealthyModuleIdentity::LaunchRole::Source;
    CHECK(!graph_->captureBaseline());
    launch.role = HealthyModuleIdentity::LaunchRole::Restore; ++launch.run;
    CHECK(!graph_->captureBaseline());
    --launch.run; launch.expectedGrantDigest[0] ^= 1;
    CHECK(!graph_->captureBaseline());
    launch.expectedGrantDigest[0] ^= 1; ++bootstrap->launch;
    CHECK(!graph_->captureBaseline());
    CHECK(!graph_->captureTarget() && !graph_->captureTargetReady());
    checkNoPlaybackAdvance();
  }
  void framebufferReplacementRevokesBaseline()
  {
    initialize(); CHECK(graph_->captureBaseline()); CHECK(renderer_->issue(true, 0));
    renderer_->replaceFramebuffer();
    CHECK(!graph_->captureTarget() && !graph_->captureTargetReady());
    checkNoPlaybackAdvance();
  }
  void revokedClaimRejectsCapture()
  {
    initialize(); CHECK(graph_->captureBaseline()); CHECK(renderer_->issue(true, 0));
    claim_->revoke();
    CHECK(!graph_->captureTarget() && !graph_->captureTargetReady());
    checkNoPlaybackAdvance();
  }
  void cleanupRetainsAccessButClosesCapture()
  {
    initialize(); CHECK(graph_->captureBaseline()); CHECK(renderer_->issue(true, 0));
    CHECK(GraphAccessOps::beginCleanupForTest(graph_->access_, 9) == graph_);
    CHECK(GraphAccessOps::cleanupGraphForTest(graph_->access_) == graph_);
    CHECK(claim_->eligible() && !claim_->open());
    CHECK(!graph_->captureTarget() && !graph_->captureTargetReady());
    checkNoPlaybackAdvance();
  }
  void rendererDestructionRejectsCapture()
  {
    initialize(); renderer_->cancelRenderer();
    CHECK(!graph_->captureBaseline());
    CHECK(!graph_->captureTarget() && !graph_->captureTargetReady());
    checkNoPlaybackAdvance();
  }
  void badBaselineRejected()
  {
    initialize(); CHECK(graph_->captureBaseline());
    CHECK(renderer_->publishBadImage(badImage_));
    CHECK(!graph_->captureTarget() && !graph_->captureTargetReady());
    checkNoPlaybackAdvance();
  }
  void replayAndContextRejected()
  {
    initialize(); CHECK(graph_->captureBaseline());
    CHECK(!renderer_->wrongContextIssue() && !renderer_->wrongEpochIssue());
    CHECK(renderer_->replayRejected());
    CHECK(!graph_->captureTarget() && !graph_->captureTargetReady());
    checkNoPlaybackAdvance();
  }
  void changedEntryRejected()
  {
    initialize(); CHECK(graph_->captureBaseline()); CHECK(renderer_->issue(true, 0));
    graph_->player_->m_nativeReceipts.start(43);
    CHECK(!graph_->captureTarget() && !graph_->captureTargetReady());
    checkNoPlaybackAdvance();
  }
  void lateDeliveryAfterContainment()
  {
    initialize(); CHECK(graph_->captureBaseline()); CHECK(renderer_->queue());
    prepared_->invalidate();
    CHECK(renderer_->lateDeliveryRejected());
    CHECK(!prepared_->graphOnGuiThread());
    CHECK(!claim_->open());
  }
  void slotInvalidatedBeforeRendererIssue()
  {
    initialize(); CHECK(graph_->captureBaseline()); CHECK(!renderer_->baselineReady(graph_->session_->attempt));
    prepared_->slot()->invalidate(9);
    CHECK(!renderer_->queue() && !renderer_->baselineReady(graph_->session_->attempt));
    CHECK(renderer_->lateDeliveryRejected());
    CHECK(!renderer_->issue(true, 0) && !renderer_->baselineReady(graph_->session_->attempt));
    CHECK(!graph_->captureTarget() && !graph_->captureTargetReady());
    checkNoPlaybackAdvance();
  }
  void slotInvalidatesQueuedTicket()
  {
    initialize(); CHECK(graph_->captureBaseline()); CHECK(renderer_->queue());
    prepared_->slot()->invalidate(9);
    CHECK(!renderer_->baselineReady(graph_->session_->attempt) && renderer_->lateDeliveryRejected());
    CHECK(!renderer_->issue(true, 0) && !renderer_->baselineReady(graph_->session_->attempt));
    CHECK(!graph_->captureTarget() && !graph_->captureTargetReady());
    checkNoPlaybackAdvance();
  }
  void failedClaimInstallationCannotCapture()
  {
    initialize(true);
    CHECK(!claim_->open() && !claim_->eligible());
    CHECK(!graph_->captureBaseline() && !graph_->captureTarget() && !graph_->captureTargetReady());
    CHECK(!graph_->requestRetirement());
    CHECK(!graph_->player_->nativeRestoreUnpauseReady());
  }
  void baselinePixelsCannotQualifyTarget()
  {
    initialize(); armTargetAndSeek();
    auto& player = *graph_->player_;
    CHECK(renderer_->issue(true, 0)); CHECK(player.healthyCapturePausedHold());
    sampleHold();
    CHECK(!player.healthyFinishPausedHold());
    CHECK(!player.nativeRestoreUnpauseReady() && !player.m_restoreUnpause.attempted);
  }
  void targetPixelsBeforeTargetArmRejected()
  {
    initialize(); CHECK(graph_->captureBaseline());
    CHECK(renderer_->issue(true, 90));
    CHECK(!graph_->captureTarget() && !graph_->captureTargetReady());
    checkNoPlaybackAdvance();
  }
  void sameIdentityRejectsOldRendererGeneration()
  {
    TestCaptureCapability::staleRendererGeneration();
  }
  int badImage_ = 0;
public:
  static void run(const QByteArray& selected)
  {
    if (selected == "uninitializedGraphRejectsCapture") { uninitializedGraphRejectsCapture(); return; }
#define RUN_CASE(name) if (selected == #name) { activeCase = #name; TestHealthyGraphCapture test; test.name(); return; }
    RUN_CASE(positiveHandoff);
    RUN_CASE(wrongBindingRejected);
    RUN_CASE(wrongLaunchBindingRejected);
    RUN_CASE(framebufferReplacementRevokesBaseline);
    RUN_CASE(revokedClaimRejectsCapture);
    RUN_CASE(cleanupRetainsAccessButClosesCapture);
    RUN_CASE(rendererDestructionRejectsCapture);
    if (selected.startsWith("badBaselineRejected-")) {
      bool ok = false; const int mode = selected.mid(20).toInt(&ok);
      CHECK(ok && mode >= 0 && mode < 3);
      activeCase = "badBaselineRejected";
      TestHealthyGraphCapture test; test.badImage_ = mode; test.badBaselineRejected(); return;
    }
    RUN_CASE(replayAndContextRejected);
    RUN_CASE(changedEntryRejected);
    RUN_CASE(lateDeliveryAfterContainment);
    RUN_CASE(slotInvalidatedBeforeRendererIssue);
    RUN_CASE(slotInvalidatesQueuedTicket);
    RUN_CASE(failedClaimInstallationCannotCapture);
    RUN_CASE(baselinePixelsCannotQualifyTarget);
    RUN_CASE(targetPixelsBeforeTargetArmRejected);
    RUN_CASE(sameIdentityRejectsOldRendererGeneration);
#undef RUN_CASE
    CHECK(false);
  }
};

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  CHECK(argc == 2);
  TestHealthyGraphCapture::run(argv[1]);
}
