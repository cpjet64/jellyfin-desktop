#include "graph.h"
#include "graph_session.h"
#include "graph_access.h"
#include "child_session.h"
#include "b_channel_test_io.h"
#include "b_channel_capture.h"
#include "RestartWire.h"
#include "player/PlayerComponent.h"
#include "player_test_cache.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace HealthyModuleIdentity;
using namespace HealthyRetirement;
using namespace FixtureRestart;
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #value); std::_Exit(3); } } while (false)

class TestHealthyGraphArm {
  static constexpr qint64 normal = 1000000000LL, overall = 31000000000LL;
  struct State {
    qint64 now = 10;
    bool moduleValid = true, revokeInModule = false, revokeInDispatch = false;
    int dispatches = 0;
  };
  PlayerTestCache cache_;
  BChannelIo pipes_;
  std::shared_ptr<State> state_ = std::make_shared<State>();
  Grant grant_{1, 2, 3, QByteArray(64, 'a'), 3000, true};
  QByteArray digest_ = QCryptographicHash::hash(encodeGrant(grant_), QCryptographicHash::Sha256);
  std::unique_ptr<PreparedGraph> prepared_;
  std::shared_ptr<ChildEndpoints> endpoints_;
  FixtureGraph* graph_ = nullptr;
  std::unique_ptr<ChildSession> session_;
  std::unique_ptr<TestCaptureCapability> renderer_;
  int context_ = 0;

  static bool moduleValid(void* opaque) noexcept
  {
    auto* state = static_cast<State*>(opaque);
    if (state->revokeInModule) {
      state->revokeInModule = false;
      bChannelScript->endpointValid = false;
    }
    return state->moduleValid;
  }

  void initialize()
  {
    bChannelScript = &pipes_; pipes_.grant = encodeGrant(grant_);
    LaunchBinding binding; binding.role = LaunchRole::Restore; binding.run = grant_.run;
    std::memcpy(binding.expectedGrantDigest.data(), digest_.constData(), size_t(digest_.size()));
    auto module = VerifiedModule::forAccessorTest(9, 7, binding, moduleValid, state_.get());
    EndpointInput inputs[] = {{EndpointRole::Grant, 1}, {EndpointRole::Control, 2}, {EndpointRole::Ack, 3}};
    auto endpoints = ChildEndpoints::createForTest(module, 9, overall, EndpointMode::Restore, inputs, bChannelEndpointOperations());
    CHECK(endpoints && !module); endpoints_ = std::shared_ptr<ChildEndpoints>(std::move(endpoints));
    const auto state = state_;
    prepared_ = FixtureGraph::prepare(9, normal, overall, [state] { return state->now; });
    CHECK(prepared_); graph_ = prepared_->graphOnGuiThread(); CHECK(graph_);
    graph_->containment_ = prepared_->containment(); graph_->module_ = endpoints_->capability();
    graph_->endpoints_ = endpoints_; graph_->access_->endpoints = endpoints_;
    graph_->player_ = std::make_unique<PlayerComponent>(); graph_->player_->m_mpvGeneration = 8;
    graph_->player_->m_restoreDispatch = [state](quint64, const char**) {
      ++state->dispatches;
      if (state->revokeInDispatch) bChannelScript->endpointValid = false;
      return 0;
    };
    graph_->player_->m_observationDispatch = [](quint64, const char**) { return 0; };
    graph_->owner_ = std::make_shared<Owner>(prepared_->containment(), overall, [state] { return state->now; },
                                             [](std::shared_ptr<RenderJob>) { return false; });
    installRenderer();
    session_ = std::make_unique<ChildSession>(*prepared_, endpoints_, 9);
    CHECK(session_->beginRestore(QStringLiteral("fixture")));
  }

  void installRenderer()
  {
    const auto slot = prepared_->slot();
    CHECK(slot->beginCreation() && slot->beforeCreate() && slot->recordCreated(&context_));
    Operations ops;
    ops.currentIdentity = [] { return Identity{11, 12, 13}; };
    ops.unregisterCallback = [](void*) {};
    ops.invalidateQueued = [](std::uint64_t) { return true; };
    ops.freeContext = [](void*) {};
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
    for (int i = 0; i < 40 && !s.gate->allowsRestore(grant_.run, 7, grant_.grant, grant_.revision, digest_, state_->now); ++i)
      CHECK(session_->pollRestore() != ChildSession::Progress::Failed);
    CHECK(s.gate->allowsRestore(grant_.run, 7, grant_.grant, grant_.revision, digest_, state_->now));
    auto& player = *graph_->player_;
    CHECK(player.healthyBindRestoreGate(s.gate, grant_.run, 7, grant_.grant, grant_.revision, digest_,
                                       [state = state_] { return state->now; }));
    CHECK(graph_->bindRestoreAuthority());
    CHECK(player.healthyRestoreSubmissionAllowed(false));
  }

  void loadCase(int mode)
  {
    initialize(); consumePermit();
    auto& player = *graph_->player_;
    const auto key = player.m_nativeRestore.key;
    if (mode == 1) pipes_.endpointValid = false;
    if (mode == 2) state_->moduleValid = false;
    if (mode == 3) state_->revokeInModule = true;
    const bool result = player.dispatchNativeRestoreLoad(QStringLiteral("fixture"));
    if (mode == 0) { CHECK(result && state_->dispatches == 1); return; }
    CHECK(!result && state_->dispatches == 0);
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
    if (lose) pipes_.endpointValid = false;
    const bool result = player.dispatchNativeRestoreSeek();
    if (!lose) { CHECK(result && state_->dispatches == 2); return; }
    CHECK(!result && state_->dispatches == 1);
    CHECK(player.m_nativeRestore.key.host == key.host && player.m_nativeRestore.key.core == key.core &&
          player.m_nativeRestore.key.revision == key.revision && player.m_nativeRestore.key.attempt == key.attempt);
    CHECK(!player.dispatchNativeRestoreSeek() && state_->dispatches == 1);
    CHECK(session_->pollRestore() == ChildSession::Progress::Failed);
    CHECK(graph_->session_->gate->canceled());
    CHECK(pipes_.writes.size() == 1 && !prepared_->closedAck());
  }

  void admittedBeforeLoss()
  {
    initialize(); consumePermit();
    auto& player = *graph_->player_;
    const auto key = player.m_nativeRestore.key;
    state_->revokeInDispatch = true;
    (void)player.dispatchNativeRestoreLoad(QStringLiteral("fixture"));
    CHECK(state_->dispatches == 1 && !pipes_.endpointValid);
    const auto receipt = player.m_nativeReceipts.snapshot();
    CHECK(receipt.key.attempt == key.attempt && receipt.submissionObserved && receipt.submitted);
    CHECK(!player.dispatchNativeRestoreLoad(QStringLiteral("fixture")) && state_->dispatches == 1);
    CHECK(session_->pollRestore() == ChildSession::Progress::Failed);
    CHECK(graph_->session_->gate->canceled());
    CHECK(pipes_.writes.size() == 1 && !prepared_->closedAck());
  }
public:
  static void run(int mode) { TestHealthyGraphArm test; test.loadCase(mode); }
  static void runSeek(bool lose) { TestHealthyGraphArm test; test.seekCase(lose); }
  static void runAdmitted() { TestHealthyGraphArm test; test.admittedBeforeLoss(); }
};

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  for (int mode = 0; mode < 4; ++mode) TestHealthyGraphArm::run(mode);
  TestHealthyGraphArm::runSeek(false);
  TestHealthyGraphArm::runSeek(true);
  TestHealthyGraphArm::runAdmitted();
  return 0;
}
