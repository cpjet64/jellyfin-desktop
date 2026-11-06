#include "graph.h"
#include "graph_session.h"
#include "graph_access.h"
#include "capture_capability.h"
#include "RestartWire.h"
#include "frame_capture.h"
#include "player/PlayerComponent.h"
#include "player_test_cache.h"
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
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #value); std::abort(); } } while (false)

class TestRetirementClaim {
public:
  static std::shared_ptr<RetirementClaim> reserve(std::uint64_t generation)
  {
    auto claim = RetirementClaim::makeArmReservation(generation);
    CHECK(claim && claim->reserveArm(generation));
    return claim;
  }
  static bool bind(const std::shared_ptr<RetirementClaim>& claim, RetirementClaim::ArmIdentity identity)
  { return claim->bindArm(claim->generation(), identity); }
};

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
    if (mode == 2) value = value.mirrored(false, true);
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
  bool replacementRejectsOldTicket(TestCaptureCapability& replacement)
  {
    if (!queued_ || !replacement.queued_ || queued_->renderer == replacement.queued_->renderer) return false;
    if (!(queued_->key == replacement.queued_->key) || queued_->request != replacement.queued_->request ||
        queued_->framebuffer != replacement.queued_->framebuffer) return false;
    const auto frame = image(0);
    replacement.bridge_->publish(*queued_, identity, frame, 0, 0, 0, 0);
    if (!fixture_restart::takeCaptures().empty()) return false;
    auto forged = *queued_; forged.serial = replacement.queued_->serial;
    replacement.bridge_->publish(forged, identity, frame, 0, 0, 0, 0);
    if (!fixture_restart::takeCaptures().empty()) return false;
    replacement.bridge_->publish(*replacement.queued_, identity, frame, 0, 0, 0, 0);
    return fixture_restart::takeCaptures().size() == 1;
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
  PlayerTestCache cache_;
  std::unique_ptr<PreparedGraph> prepared_;
  FixtureGraph* graph_ = nullptr;
  std::shared_ptr<RetirementClaim> claim_;
  std::unique_ptr<TestCaptureCapability> renderer_;
  int context_ = 0;
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
  void installRenderer()
  {
    const auto slot = prepared_->slot();
    CHECK(slot->beginCreation() && slot->beforeCreate() && slot->recordCreated(&context_));
    HealthyRetirement::Operations ops;
    ops.currentIdentity = [] { return Identity{11, 12, 13}; };
    ops.unregisterCallback = [](void*) {};
    ops.invalidateQueued = [](std::uint64_t) { return true; };
    ops.freeContext = [](void*) {};
    ops.clearPointer = [](void*) { return true; };
    CHECK(slot->install({11, 12, 13}, &context_, ops));
    renderer_ = std::make_unique<TestCaptureCapability>(slot);
    CHECK(renderer_->initialize());
  }
  void installPlayer(bool failClaim)
  {
    graph_->player_ = std::make_unique<PlayerComponent>();
    auto& player = *graph_->player_;
    graph_->session_ = std::make_unique<FixtureGraph::Session>();
    auto gate = std::make_shared<ChildGate>(&context_, graph_->deadline_);
    Grant grant{1, 3, 4, QByteArray(64, 'a'), 3000, false};
    const auto encoded = encodeGrant(grant);
    const auto digest = QCryptographicHash::hash(encoded, QCryptographicHash::Sha256);
    CHECK(gate->accept(&context_, encoded, grant, digest, now(), true));
    const RestorePermit permit{1, 5, 3, 4, 1, digest};
    CHECK(gate->installPermit(encodeRestorePermit(permit), permit, now()));
    CHECK(player.healthyBindRestoreCleanupGate(gate, graph_->deadline_));
    CHECK(player.bindNativeReceiptHost(7)); player.m_mpvGeneration = 8;
    claim_ = TestRetirementClaim::reserve(9);
    const PlayerComponent::RestoreGrant restore{3, 4, 0, 3000, false};
    CHECK(player.armNativeRestore(restore, 7, 2));
    const auto key = player.m_nativeRestore.key;
    CHECK(TestRetirementClaim::bind(claim_, {key.host, key.core, key.revision, key.attempt}));
    std::atomic_store(&graph_->access_->intentClaim, claim_);
    CHECK(player.healthyInstallRetirementClaim(claim_, 9, HealthyRestart::ClaimInstallMode::BindRestoreReservation));
    if (failClaim) setGraphInstallHookForTest([] { return false; });
    const bool installed = graph_->installRetirementClaim();
    if (failClaim) setGraphInstallHookForTest({});
    CHECK(installed != failClaim);
    if (failClaim) return;
    player.m_restoreDispatch = [](quint64, const char**) { return 0; };
    player.m_observationDispatch = [](quint64, const char**) { return 0; };
    CHECK(player.dispatchNativeRestoreLoad(QStringLiteral("fixture")));
    player.m_nativeReceipts.reply(2, true, 42); player.m_nativeReceipts.start(42);
    CHECK(player.healthyBindRestoreGate(gate, 1, 5, 3, 4, digest, now));
    auto& session = *graph_->session_;
    session.gate = gate; session.host = key.host; session.core = key.core;
    session.intentRevision = key.revision; session.attempt = key.attempt;
    session.binding = grant; session.run = 1; session.launch = 5; session.digest = digest;
    HealthyModuleIdentity::LaunchBinding launch;
    launch.role = HealthyModuleIdentity::LaunchRole::Restore; launch.run = 1;
    std::memcpy(launch.expectedGrantDigest.data(), digest.constData(), size_t(digest.size()));
    graph_->captureBindingForTest_ = launch; graph_->captureLaunchForTest_ = 5;
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
    fixture_restart::disarmCapture();
    prepared_ = prepare(); CHECK(prepared_);
    graph_ = prepared_->graphOnGuiThread(); CHECK(graph_);
    graph_->captureSessionReadyForTest_ = [] { return true; };
    installRenderer(); installPlayer(failClaim);
    if (!failClaim) observe(1, "0.000");
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
    event.reply_userdata = (quint64{1} << 61) | 2; player.observeNativeReceipt(&event);
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
    auto& launch = *graph_->captureBindingForTest_;
    launch.role = HealthyModuleIdentity::LaunchRole::Source;
    CHECK(!graph_->captureBaseline());
    launch.role = HealthyModuleIdentity::LaunchRole::Restore; ++launch.run;
    CHECK(!graph_->captureBaseline());
    --launch.run; launch.expectedGrantDigest[0] ^= 1;
    CHECK(!graph_->captureBaseline());
    launch.expectedGrantDigest[0] ^= 1; ++graph_->captureLaunchForTest_;
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
    initialize(); CHECK(graph_->captureBaseline()); CHECK(renderer_->queue());
    TestHealthyGraphCapture replacement; replacement.initialize();
    CHECK(replacement.graph_->captureBaseline()); CHECK(replacement.renderer_->queue());
    CHECK(renderer_->replacementRejectsOldTicket(*replacement.renderer_));
    CHECK(!replacement.graph_->captureTargetReady());
    replacement.checkNoPlaybackAdvance();
  }
  int badImage_ = 0;
public:
  static void run()
  {
    uninitializedGraphRejectsCapture();
    { TestHealthyGraphCapture test; test.positiveHandoff(); }
    { TestHealthyGraphCapture test; test.wrongBindingRejected(); }
    { TestHealthyGraphCapture test; test.wrongLaunchBindingRejected(); }
    { TestHealthyGraphCapture test; test.framebufferReplacementRevokesBaseline(); }
    { TestHealthyGraphCapture test; test.revokedClaimRejectsCapture(); }
    { TestHealthyGraphCapture test; test.cleanupRetainsAccessButClosesCapture(); }
    { TestHealthyGraphCapture test; test.rendererDestructionRejectsCapture(); }
    for (int mode = 0; mode != 3; ++mode) {
      TestHealthyGraphCapture test; test.badImage_ = mode; test.badBaselineRejected();
    }
    { TestHealthyGraphCapture test; test.replayAndContextRejected(); }
    { TestHealthyGraphCapture test; test.changedEntryRejected(); }
    { TestHealthyGraphCapture test; test.lateDeliveryAfterContainment(); }
    { TestHealthyGraphCapture test; test.failedClaimInstallationCannotCapture(); }
    { TestHealthyGraphCapture test; test.baselinePixelsCannotQualifyTarget(); }
    { TestHealthyGraphCapture test; test.targetPixelsBeforeTargetArmRejected(); }
    { TestHealthyGraphCapture test; test.sameIdentityRejectsOldRendererGeneration(); }
  }
};

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  TestHealthyGraphCapture::run();
}
