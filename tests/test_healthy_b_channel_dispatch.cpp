#include "graph.h"
#include "graph_session.h"
#include "graph_access.h"
#include "child_session.h"
#include "healthy_restart/b_channel_test_io.h"
#include "healthy_restart/b_channel_capture.h"
#include "RestartWire.h"
#include "player/PlayerComponent.h"
#include "player_test_cache.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>

using namespace HealthyModuleIdentity;
using namespace HealthyRetirement;
using namespace FixtureRestart;
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #value); std::_Exit(3); } } while (false)

class TestHealthyGraphArm {
  static qint64 now()
  {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  static qint64 deadline(qint64 start, qint64 interval)
  {
    CHECK(start >= 0 && interval > 0 && start <= std::numeric_limits<qint64>::max() - interval);
    return start + interval;
  }
  static constexpr qint64 endpointDeadline = 31000000000LL;
  struct State {
    std::function<void()> revokeInModule, revokeInDispatch;
    int dispatches = 0, frees = 0;
    std::vector<std::vector<QByteArray>> commands;
    std::vector<quint64> commandIds;
  };
  PlayerTestCache cache_;
  BChannelIo pipes_;
  const qint64 start_ = now();
  const qint64 normal_ = deadline(start_, 1000000000LL);
  const qint64 overall_ = deadline(start_, 31000000000LL);
  std::shared_ptr<State> state_ = std::make_shared<State>();
  Grant grant_{1, 2, 3, QByteArray(64, 'a'), 3000, true};
  QByteArray digest_ = QCryptographicHash::hash(encodeGrant(grant_), QCryptographicHash::Sha256);
  std::unique_ptr<PreparedGraph> prepared_;
  std::shared_ptr<ChildEndpoints> endpoints_;
  FixtureGraph* graph_ = nullptr;
  std::unique_ptr<ChildSession> session_;
  std::unique_ptr<TestCaptureCapability> renderer_;
  int context_ = 0;
  bool reentryArmed_ = false, reentryFired_ = false;

  static bool moduleValid(void* opaque) noexcept
  {
    auto* state = static_cast<State*>(opaque);
    if (state->revokeInModule) {
      auto revoke = std::move(state->revokeInModule);
      revoke();
    }
    return true;
  }

  void initialize(int reentry = 0)
  {
    bChannelScript = &pipes_; pipes_.grant = encodeGrant(grant_);
    LaunchBinding binding; binding.role = LaunchRole::Restore; binding.run = grant_.run;
    std::memcpy(binding.expectedGrantDigest.data(), digest_.constData(), size_t(digest_.size()));
    auto module = VerifiedModule::forAccessorTest(9, 7, binding, moduleValid, state_.get());
    EndpointInput inputs[] = {{EndpointRole::Grant, 1}, {EndpointRole::Control, 2}, {EndpointRole::Ack, 3}};
    auto ops = bChannelEndpointOperations();
    ops.valid = [this, reentry] {
      if (reentryArmed_ && reentry && graph_) {
        const bool hasGate = graph_->session_ && graph_->session_->gate;
        if (hasGate == (reentry == 2)) {
          reentryArmed_ = false; reentryFired_ = true; session_->invalidate();
        }
      }
      return pipes_.endpointValid;
    };
    auto endpoints = ChildEndpoints::createForTest(module, 9, endpointDeadline, EndpointMode::Restore, inputs, ops);
    CHECK(endpoints && !module); endpoints_ = std::shared_ptr<ChildEndpoints>(std::move(endpoints));
    const auto state = state_;
    prepared_ = FixtureGraph::prepare(9, normal_, overall_, now);
    CHECK(prepared_); graph_ = prepared_->graphOnGuiThread(); CHECK(graph_);
    graph_->containment_ = prepared_->containment(); graph_->module_ = endpoints_->capability();
    graph_->endpoints_ = endpoints_; graph_->access_->endpoints = endpoints_;
    graph_->player_ = std::make_unique<PlayerComponent>(); graph_->player_->m_mpvGeneration = 8;
    graph_->player_->m_restoreDispatch = [state](quint64 id, const char** args) {
      ++state->dispatches;
      state->commandIds.push_back(id);
      state->commands.emplace_back();
      for (; *args; ++args) state->commands.back().emplace_back(*args);
      if (state->revokeInDispatch) {
        auto revoke = std::move(state->revokeInDispatch);
        revoke();
      }
      return 0;
    };
    graph_->player_->m_observationDispatch = [](quint64, const char**) { return 0; };
    graph_->owner_ = std::make_shared<Owner>(prepared_->containment(), overall_, now,
                                             [](std::shared_ptr<RenderJob>) { return false; });
    installRenderer();
    session_ = std::make_unique<ChildSession>(*prepared_, endpoints_, 9);
    reentryArmed_ = reentry != 0;
    const bool started = session_->beginRestore(QStringLiteral("fixture"));
    CHECK(started == (reentry == 0));
  }

  void installRenderer()
  {
    const auto slot = prepared_->slot();
    const auto module = graph_->module_;
    const std::shared_ptr<const std::atomic_bool> revoked(graph_->access_, &graph_->access_->revoked);
    CHECK(slot->bindModuleForTest([module] { return module && module->stillValid(9); }, revoked));
    CHECK(slot->beginCreation() && slot->beforeCreate() && slot->recordCreated(&context_));
    HealthyRetirement::Operations ops;
    ops.currentIdentity = [] { return Identity{11, 12, 13}; };
    ops.unregisterCallback = [](void*) {};
    ops.invalidateQueued = [](std::uint64_t) { return true; };
    const auto state = state_;
    ops.freeContext = [state](void*) { ++state->frees; };
    ops.clearPointer = [](void*) { return true; };
    CHECK(slot->install({11, 12, 13}, &context_, ops));
    renderer_ = std::make_unique<TestCaptureCapability>(slot);
    CHECK(renderer_->initialize());
  }

  void consumePermit()
  {
    ChildSession::Progress progress = ChildSession::Progress::Pending;
    for (int i = 0; i < 40 && progress != ChildSession::Progress::Ready; ++i) {
      progress = session_->pollRestore(); CHECK(progress != ChildSession::Progress::Failed);
    }
    CHECK(progress == ChildSession::Progress::Ready);
    CHECK(pipes_.writes.size() == 1 && pipes_.writes[0].left(4) == "JGA2");
    GrantAccepted accepted;
    const auto& s = *graph_->session_;
    const GrantAccepted expected{grant_.run, 7, grant_.grant, grant_.revision, digest_,
                                 s.host, s.core, s.intentRevision, s.attempt};
    CHECK(decodeGrantAccepted(pipes_.writes[0], expected, accepted));
    CHECK(accepted.host == s.host && accepted.core == s.core &&
          accepted.intentRevision == s.intentRevision && accepted.attempt == s.attempt);
    pipes_.control = encodeRestorePermit({grant_.run, 7, grant_.grant, grant_.revision, 1, digest_});
    for (int i = 0; i < 40 && !s.gate->allowsRestore(grant_.run, 7, grant_.grant, grant_.revision, digest_, now()); ++i)
      CHECK(session_->pollRestore() != ChildSession::Progress::Failed);
    CHECK(s.gate->allowsRestore(grant_.run, 7, grant_.grant, grant_.revision, digest_, now()));
    auto& player = *graph_->player_;
    CHECK(player.m_restoreGate == s.gate && player.m_healthyRestoreAuthority);
    CHECK(player.healthyRestoreSubmissionAllowed(false));
  }

  void loadCase(int mode)
  {
    initialize(); consumePermit();
    auto& player = *graph_->player_;
    const auto key = player.m_nativeRestore.key;
    const auto claim = std::atomic_load(&graph_->access_->intentClaim);
    CHECK(claim && claim->open());
    if (mode == 1) loseEndpoint();
    if (mode == 2) endpoints_->capability()->invalidateAdmissionForTest();
    if (mode == 3) state_->revokeInModule = [this] { loseEndpoint(); };
    const bool result = player.dispatchNativeRestoreLoad(QStringLiteral("fixture"));
    if (mode == 0) {
      CHECK(result && state_->dispatches == 1);
      const auto& command = state_->commands[0];
      CHECK(command.size() >= 4 && command.front() == "loadfile" && command.back() == "pause=yes");
      CHECK(state_->commandIds[0] == ((quint64{1} << 63) | key.attempt));
      return;
    }
    CHECK(!result && state_->dispatches == 0);
    CHECK(!claim->open());
    CHECK(player.m_nativeRestore.key.host == key.host && player.m_nativeRestore.key.core == key.core &&
          player.m_nativeRestore.key.revision == key.revision && player.m_nativeRestore.key.attempt == key.attempt);
    CHECK(!player.dispatchNativeRestoreLoad(QStringLiteral("fixture")) && state_->dispatches == 0);
    CHECK(session_->pollRestore() == ChildSession::Progress::Failed);
    CHECK(graph_->session_->gate->canceled());
    CHECK(pipes_.writes.size() == 1 && !prepared_->closedAck());
  }

  void observePaused()
  {
    auto& player = *graph_->player_;
    const auto key = player.m_nativeReceipts.snapshot().key;
    CHECK(player.requestNativeObservation(key, 1, 10000, true));
    QByteArray text = "NR1|1|42|yes|yes|0.000|yes|no|no";
    mpv_event_command command{}; command.result.format = MPV_FORMAT_STRING; command.result.u.string = text.data();
    mpv_event event{}; event.event_id = MPV_EVENT_COMMAND_REPLY;
    event.reply_userdata = (quint64{1} << 62) | 1; event.data = &command;
    player.observeNativeReceipt(&event);
    CHECK(player.m_nativeObservation.valid && player.m_nativeObservation.paused);
  }

  void seekCase(bool lose)
  {
    initialize(); consumePermit();
    auto& player = *graph_->player_;
    CHECK(player.dispatchNativeRestoreLoad(QStringLiteral("fixture")) && state_->dispatches == 1);
    const auto key = player.m_nativeRestore.key;
    player.m_nativeReceipts.reply(key.attempt, true, 42); player.m_nativeReceipts.start(42);
    observePaused();
    CHECK(graph_->captureBaseline());
    CHECK(!graph_->captureTarget());
    CHECK(renderer_->issue(true));
    CHECK(graph_->captureTarget());
    CHECK(renderer_->issue(false) && graph_->captureTargetReady());
    CHECK(player.healthyRestoreSubmissionAllowed(true));
    const auto claim = std::atomic_load(&graph_->access_->intentClaim);
    CHECK(claim && claim->open());
    if (lose) loseEndpoint();
    const bool result = player.dispatchNativeRestoreSeek();
    if (!lose) {
      CHECK(result && state_->dispatches == 2);
      const auto& command = state_->commands[1];
      CHECK(command.size() == 3 && command[0] == "seek" && command[1] == "3.000" && command[2] == "absolute+exact");
      CHECK(state_->commandIds[1] == ((quint64{1} << 61) | key.attempt));
      return;
    }
    CHECK(!result && state_->dispatches == 1);
    CHECK(!claim->open());
    CHECK(player.m_nativeRestore.key.host == key.host && player.m_nativeRestore.key.core == key.core &&
          player.m_nativeRestore.key.revision == key.revision && player.m_nativeRestore.key.attempt == key.attempt);
    CHECK(!player.dispatchNativeRestoreSeek() && state_->dispatches == 1);
    CHECK(session_->pollRestore() == ChildSession::Progress::Failed);
    CHECK(graph_->session_->gate->canceled());
    CHECK(pipes_.writes.size() == 1 && !prepared_->closedAck());
  }

  void admittedBeforeLoss(bool successfulReply)
  {
    initialize(); consumePermit();
    auto& player = *graph_->player_;
    const auto key = player.m_nativeRestore.key;
    state_->revokeInDispatch = [this] { loseEndpoint(); };
    (void)player.dispatchNativeRestoreLoad(QStringLiteral("fixture"));
    CHECK(state_->dispatches == 1 && !endpoints_->token(EndpointRole::Control));
    const auto receipt = player.m_nativeReceipts.snapshot();
    CHECK(receipt.key.attempt == key.attempt && receipt.submissionObserved && receipt.submitted);
    CHECK(!player.dispatchNativeRestoreLoad(QStringLiteral("fixture")) && state_->dispatches == 1);
    CHECK(session_->pollRestore() == ChildSession::Progress::Failed);
    CHECK(graph_->session_->gate->canceled());
    char entryName[] = "playlist_entry_id";
    char* names[] = {entryName};
    mpv_node entry{}; entry.format = MPV_FORMAT_INT64; entry.u.int64 = 42;
    mpv_node_list list{}; list.num = 1; list.keys = names; list.values = &entry;
    mpv_event_command command{}; command.result.format = MPV_FORMAT_NODE_MAP; command.result.u.list = &list;
    mpv_event reply{}; reply.event_id = MPV_EVENT_COMMAND_REPLY; reply.error = successfulReply ? 0 : -1;
    if (successfulReply) reply.data = &command;
    reply.reply_userdata = (quint64{1} << 63) | key.attempt;
    player.observeNativeReceipt(&reply);
    const auto settled = player.m_nativeReceipts.snapshot();
    CHECK(settled.key.host == key.host && settled.key.core == key.core &&
          settled.key.revision == key.revision && settled.key.attempt == key.attempt);
    CHECK(settled.submissionObserved && settled.submitted && settled.reply && settled.replyOk == successfulReply);
    CHECK(!player.healthyRestoreSubmissionAllowed(false));
    CHECK(!player.dispatchNativeRestoreLoad(QStringLiteral("fixture")) && state_->dispatches == 1);
    CHECK(pipes_.writes.size() == 1 && !prepared_->closedAck());
  }
  void loseEndpoint()
  {
    endpoints_->invalidateAdmissionForTest();
  }
  void checkFailed(const std::shared_ptr<ChildGate>& gate,
                   const std::shared_ptr<RetirementClaim>& claim, int commands)
  {
    CHECK(gate && gate->canceled() && claim && !claim->open() && !claim->eligible());
    CHECK(graph_->session_->gate == gate);
    CHECK(graph_->session_->firstFailure == HealthyRestart::FailureCause::OwnershipLost);
    CHECK(session_->pollFailureCleanup() == ChildSession::Progress::Failed);
    CHECK(!session_->load() && !session_->seek());
    CHECK(state_->dispatches == commands && state_->frees == 0);
    CHECK(pipes_.writes.size() == 1 && !prepared_->closedAck());
  }
  void failureOrder(bool containmentFirst)
  {
    initialize(); consumePermit();
    const auto gate = graph_->session_->gate;
    const auto claim = std::atomic_load(&graph_->access_->intentClaim);
    CHECK(gate && !gate->canceled() && claim && claim->open());
    if (containmentFirst) endpoints_->beginContainment();
    session_->invalidate();
    if (!containmentFirst) endpoints_->beginContainment();
    checkFailed(gate, claim, 0);
    session_->invalidate();
    checkFailed(gate, claim, 0);
  }
  void pendingLoadExpires()
  {
    initialize(); consumePermit();
    const auto gate = graph_->session_->gate;
    const auto claim = std::atomic_load(&graph_->access_->intentClaim);
    CHECK(graph_->restoreLoad() && state_->dispatches == 1);
    CHECK(session_->pollPlayback() == ChildSession::Progress::Pending);
    graph_->clock_ = [this] { return normal_ + 1; };
    CHECK(session_->pollPlayback() == ChildSession::Progress::Failed);
    checkFailed(gate, claim, 1);
  }
  void reentrantInitialization(int stage)
  {
    initialize(stage);
    CHECK(reentryFired_ && !reentryArmed_);
    if (graph_->session_ && graph_->session_->gate) {
      CHECK(graph_->session_->gate->canceled());
      CHECK(graph_->session_->firstFailure == HealthyRestart::FailureCause::OwnershipLost);
    } else CHECK(stage == 1);
    session_->invalidate();
    CHECK(session_->pollFailureCleanup() == ChildSession::Progress::Failed);
    CHECK(state_->dispatches == 0 && state_->frees == 0 && pipes_.writes.empty());
    CHECK(!prepared_->closedAck());
  }
public:
  static void run(int mode) { TestHealthyGraphArm test; test.loadCase(mode); }
  static void runSeek(bool lose) { TestHealthyGraphArm test; test.seekCase(lose); }
  static void runAdmitted(bool success) { TestHealthyGraphArm test; test.admittedBeforeLoss(success); }
  static void runFailureOrder(bool first) { TestHealthyGraphArm test; test.failureOrder(first); }
  static void runPendingLoadExpires() { TestHealthyGraphArm test; test.pendingLoadExpires(); }
  static void runReentrant(int stage) { TestHealthyGraphArm test; test.reentrantInitialization(stage); }
};

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  CHECK(argc == 2);
  const QByteArray name(argv[1]);
  if (name == "load-ok") TestHealthyGraphArm::run(0);
  else if (name == "load-endpoint") TestHealthyGraphArm::run(1);
  else if (name == "load-module") TestHealthyGraphArm::run(2);
  else if (name == "load-reentrant") TestHealthyGraphArm::run(3);
  else if (name == "seek-ok") TestHealthyGraphArm::runSeek(false);
  else if (name == "seek-endpoint") TestHealthyGraphArm::runSeek(true);
  else if (name == "admitted-failed") TestHealthyGraphArm::runAdmitted(false);
  else if (name == "admitted-ok") TestHealthyGraphArm::runAdmitted(true);
  else if (name == "containment-first") TestHealthyGraphArm::runFailureOrder(true);
  else if (name == "failure-first") TestHealthyGraphArm::runFailureOrder(false);
  else if (name == "pending-load-expires") TestHealthyGraphArm::runPendingLoadExpires();
  else if (name == "reentry-before-gate") TestHealthyGraphArm::runReentrant(1);
  else if (name == "reentry-after-gate") TestHealthyGraphArm::runReentrant(2);
  else CHECK(false);
  return 0;
}
