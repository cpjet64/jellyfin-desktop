#include "healthy_restart/graph.h"
#include "healthy_restart/graph_session.h"
#include "healthy_restart/module_endpoints.h"
#include "fixture_restart/RestartWire.h"
#include "player/PlayerComponent.h"
#include "player_test_cache.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <cstdlib>
#include <cstring>

using namespace HealthyModuleIdentity;
using namespace HealthyRetirement;
using namespace FixtureRestart;
static void check(bool value) { if (!value) std::abort(); }

namespace {
HANDLE WINAPI event(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR) { return reinterpret_cast<HANDLE>(99); }
BOOL WINAPI yesHandle(HANDLE) { return TRUE; }
ULONGLONG WINAPI nowIo() { return 1; }
BOOL WINAPI readIo(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED) { return FALSE; }
BOOL WINAPI writeIo(HANDLE, LPCVOID, DWORD size, LPDWORD count, LPOVERLAPPED) { *count = size; return TRUE; }
BOOL WINAPI resultIo(HANDLE, LPOVERLAPPED, LPDWORD, BOOL) { return FALSE; }
BOOL WINAPI cancelIo(HANDLE, LPOVERLAPPED) { return TRUE; }
DWORD WINAPI errorIo() { return ERROR_IO_PENDING; }
bool validModule(void*) noexcept { return true; }

ChildEndpoints::TestOperations endpointOperations()
{
  ChildEndpoints::TestOperations ops;
  ops.valid = [] { return true; };
  ops.accepts = [](std::uintptr_t value) { return value != 0; };
  ops.compare = [](std::uintptr_t a, std::uintptr_t b) {
    return a == b ? EndpointComparison::Same : EndpointComparison::Different;
  };
  ops.duplicate = [](std::uintptr_t value, HANDLE& out) { out = reinterpret_cast<HANDLE>(value + 100); return true; };
  ops.close = [](HANDLE) { return true; };
  ops.io.createEvent = event; ops.io.closeHandle = yesHandle; ops.io.resetEvent = yesHandle;
  ops.io.now = nowIo; ops.io.readFile = readIo; ops.io.writeFile = writeIo;
  ops.io.result = resultIo; ops.io.cancel = cancelIo; ops.io.error = errorIo;
  return ops;
}
}

class TestHealthyGraphArm {
  static constexpr qint64 normal = 1000000000LL, overall = 31000000000LL;
  PlayerTestCache cache_;
  struct State {
    qint64 now = 10;
    FixtureGraph* graph = nullptr;
    int dispatches = 0;
    quint64 idleQuery = 0, positionQuery = 0;
    mpv_event noEvent{MPV_EVENT_NONE, 0, 0, nullptr};
    bool cancelOnArm = false, cancelBeforeArm = false, expireOnArm = false;
    bool inClock = false, cancelCalled = false;
    QByteArray cancelResponse;
  };
  std::shared_ptr<State> state_ = std::make_shared<State>();
  Grant grant_{1, 2, 3, QByteArray(64, 'a'), 3000, true};
  QByteArray digest_ = QCryptographicHash::hash(encodeGrant(grant_), QCryptographicHash::Sha256);
  std::unique_ptr<PreparedGraph> prepared_;
  FixtureGraph* graph_ = nullptr;
  static qint64 clock(const std::shared_ptr<State>& state)
  {
    if (!state->cancelOnArm && !state->cancelBeforeArm && !state->expireOnArm) return state->now;
    const auto* session = state->graph ? state->graph->session_.get() : nullptr;
    const bool before = state->cancelBeforeArm && session && session->gate && session->gate->acceptedGrant() && !session->host;
    const bool after = state->cancelOnArm && session && session->host;
    if (state->expireOnArm && session && session->host) { state->expireOnArm = false; state->now = normal; }
    if (!state->inClock && (before || after)) {
      state->inClock = true; state->cancelOnArm = false; state->cancelBeforeArm = false;
      state->cancelCalled = state->graph->restoreControl(encodeControl({ControlKind::UserStop, 1, 4}), state->cancelResponse);
      state->inClock = false;
    }
    return state->now;
  }

  void initialize()
  {
    LaunchBinding binding;
    binding.role = LaunchRole::Restore; binding.run = grant_.run;
    std::memcpy(binding.expectedGrantDigest.data(), digest_.constData(), size_t(digest_.size()));
    auto module = VerifiedModule::forAccessorTest(9, 7, binding, validModule, nullptr);
    EndpointInput inputs[] = {{EndpointRole::Grant, 1}, {EndpointRole::Control, 2}, {EndpointRole::Ack, 3}};
    auto endpoints = ChildEndpoints::createForTest(module, 9, overall, EndpointMode::Restore, inputs, endpointOperations());
    check(endpoints && !module);
    auto shared = std::shared_ptr<ChildEndpoints>(std::move(endpoints));
    const auto state = state_;
    prepared_ = FixtureGraph::prepare(9, normal, overall, [state] { return clock(state); });
    check(bool(prepared_)); graph_ = prepared_->graphOnGuiThread(); check(graph_);
    state_->graph = graph_;
    graph_->containment_ = prepared_->containment();
    graph_->module_ = shared->capability(); graph_->endpoints_ = std::move(shared);
    graph_->player_ = std::make_unique<PlayerComponent>();
    graph_->player_->m_mpvGeneration = 8;
    graph_->player_->m_restoreDispatch = [state](quint64, const char**) { ++state->dispatches; return 0; };
    graph_->player_->m_healthyStopClock = [state] { return state->now; };
    graph_->player_->m_healthyStopEventWait = [state] { return &state->noEvent; };
    graph_->player_->m_healthyStopDispatch = [state](quint64 id, const char** args, bool query) {
      if (!query) return -1;
      if (!std::strcmp(args[0], "idle-active")) state->idleQuery = id;
      else if (!std::strcmp(args[0], "playlist-pos")) state->positionQuery = id;
      else return -1;
      return 0;
    };
    graph_->owner_ = std::make_shared<Owner>(prepared_->containment(), overall, [state] { return state->now; },
                                             [](std::shared_ptr<RenderJob>) { return false; });
    check(graph_->restoreInitialize(QString()));
  }

  void cancelBeforeGrant()
  {
    initialize(); QByteArray response;
    check(graph_->restoreControl(encodeControl({ControlKind::UserStop, 1, 4}), response));
    check(decodeCancelNoArm(response, {grant_.run, 7, 4, 1, digest_}));
    response.clear(); check(!graph_->restoreGrant(encodeGrant(grant_), true, response));
    check(response.isEmpty() && !graph_->restoreLoad() && !graph_->restoreSeek() && state_->dispatches == 0);
  }
  GrantAccepted expectedAcceptance() const
  {
    const auto& s = *graph_->session_;
    return {grant_.run, 7, grant_.grant, grant_.revision, digest_, s.host, s.core, s.intentRevision, s.attempt};
  }
  void settleNoLoad(QByteArray& response)
  {
    if (!state_->idleQuery || !state_->positionQuery) check(graph_->restorePollCancellation(response));
    check(response.isEmpty() && state_->idleQuery && state_->positionQuery);
    int idle = 1; mpv_event_property idleProperty{"idle-active", MPV_FORMAT_FLAG, &idle};
    mpv_event idleReply{MPV_EVENT_GET_PROPERTY_REPLY, 0, state_->idleQuery, &idleProperty};
    graph_->player_->observeNativeRestoreCleanup(&idleReply);
    graph_->player_->observeNativeReceipt(&idleReply);
    int64_t position = -1; mpv_event_property positionProperty{"playlist-pos", MPV_FORMAT_INT64, &position};
    mpv_event positionReply{MPV_EVENT_GET_PROPERTY_REPLY, 0, state_->positionQuery, &positionProperty};
    graph_->player_->observeNativeRestoreCleanup(&positionReply);
    graph_->player_->observeNativeReceipt(&positionReply);
    for (int i = 0; i != 8 && response.isEmpty(); ++i) check(graph_->restorePollCancellation(response));
  }
  void deadlineAfterArm()
  {
    initialize(); state_->expireOnArm = true; QByteArray response;
    check(graph_->restoreGrant(encodeGrant(grant_), true, response));
    GrantAccepted accepted;
    check(decodeGrantAccepted(response, expectedAcceptance(), accepted));
    check(accepted.host && accepted.core == 8 && accepted.intentRevision && accepted.attempt);
    const auto key = graph_->player_->m_nativeRestore.key;
    check(accepted.host == key.host && accepted.core == key.core &&
          accepted.intentRevision == key.revision && accepted.attempt == key.attempt);
    check(state_->now == normal); response.clear();
    check(graph_->restoreControl(encodeControl({ControlKind::UserStop, 1, 4}), response));
    settleNoLoad(response);
    CancelResult result;
    const CancelResult expected{grant_.run, 7, grant_.grant, grant_.revision, 1,
      accepted.host, accepted.core, accepted.intentRevision, accepted.attempt};
    check(decodeCancelResult(response, expected, result));
    check(result.outcome == CancelOutcome::NoLoad && !graph_->restoreLoad() && !graph_->restoreSeek());
    check(state_->dispatches == 0);
  }
  void claimFailureRetainsIdentity()
  {
    initialize();
    std::atomic_store(&graph_->access_->intentClaim, std::make_shared<RetirementClaim>(9));
    QByteArray response;
    check(!graph_->restoreGrant(encodeGrant(grant_), true, response));
    const auto& s = *graph_->session_;
    check(response.isEmpty() && s.host && s.core == 8 && s.intentRevision && s.attempt);
    const auto key = graph_->player_->m_nativeRestore.key;
    check(s.host == key.host && s.core == key.core && s.intentRevision == key.revision && s.attempt == key.attempt);
    check(!graph_->restoreLoad() && !graph_->restoreSeek() && state_->dispatches == 0);
  }
  void reentrantCancelAfterArm()
  {
    initialize(); state_->cancelOnArm = true; QByteArray response;
    check(graph_->restoreGrant(encodeGrant(grant_), true, response) && state_->cancelCalled);
    GrantAccepted accepted;
    check(decodeGrantAccepted(response, expectedAcceptance(), accepted));
    const auto key = graph_->player_->m_nativeRestore.key;
    check(accepted.host == key.host && accepted.core == key.core &&
          accepted.intentRevision == key.revision && accepted.attempt == key.attempt);
    settleNoLoad(state_->cancelResponse);
    CancelResult result;
    const CancelResult expected{grant_.run, 7, grant_.grant, grant_.revision, 1,
      accepted.host, accepted.core, accepted.intentRevision, accepted.attempt};
    check(decodeCancelResult(state_->cancelResponse, expected, result));
    check(result.outcome == CancelOutcome::NoLoad && state_->dispatches == 0);
    check(!graph_->restoreLoad() && !graph_->restoreSeek());
  }
  void cancelAfterValidationBeforeArm()
  {
    initialize(); state_->cancelBeforeArm = true; QByteArray response;
    check(graph_->restoreGrant(encodeGrant(grant_), true, response) && state_->cancelCalled);
    check(state_->cancelResponse.isEmpty());
    check(decodeCancelNoArm(response, {grant_.run, 7, 4, 1, digest_}));
    check(!graph_->session_->host && !graph_->session_->attempt && state_->dispatches == 0);
    check(!graph_->restoreLoad() && !graph_->restoreSeek());
  }
  void duplicateCancellationRejected()
  {
    initialize(); QByteArray response;
    const auto request = encodeControl({ControlKind::UserStop, 1, 4});
    check(graph_->restoreControl(request, response));
    check(decodeCancelNoArm(response, {grant_.run, 7, 4, 1, digest_}));
    response.clear();
    check(!graph_->restoreControl(request, response) && response.isEmpty());
    check(!graph_->restoreLoad() && !graph_->restoreSeek() && state_->dispatches == 0);
  }
  void changedCoreCannotSettle()
  {
    initialize(); QByteArray response;
    check(graph_->restoreGrant(encodeGrant(grant_), true, response));
    GrantAccepted accepted;
    check(decodeGrantAccepted(response, expectedAcceptance(), accepted));
    graph_->player_->m_mpvGeneration = accepted.core + 1;
    response.clear();
    const bool started = graph_->restoreControl(encodeControl({ControlKind::UserStop, 1, 4}), response);
    if (started && response.isEmpty()) graph_->restorePollCancellation(response);
    CancelResult result;
    const CancelResult expected{grant_.run, 7, grant_.grant, grant_.revision, 1,
      accepted.host, accepted.core, accepted.intentRevision, accepted.attempt};
    check(response.isEmpty() || (decodeCancelResult(response, expected, result) &&
                                 result.outcome == CancelOutcome::Failed));
    check(state_->dispatches == 0 && !graph_->restoreLoad() && !graph_->restoreSeek());
  }
  void wrongGateRejected()
  {
    initialize(); QByteArray response;
    check(graph_->restoreGrant(encodeGrant(grant_), true, response));
    auto wrong = std::make_shared<ChildGate>(graph_->endpoints_->token(EndpointRole::Grant), normal);
    check(wrong->cancel([](void*) { return true; }, nullptr));
    check(!graph_->player_->beginNativeRestoreCancellation(wrong, 1, overall));
    check(state_->dispatches == 0);
    check(!graph_->restoreLoad() && !graph_->restoreSeek() && state_->dispatches == 0);
  }
public:
  static void run()
  {
    { TestHealthyGraphArm test; test.cancelBeforeGrant(); }
    { TestHealthyGraphArm test; test.deadlineAfterArm(); }
    { TestHealthyGraphArm test; test.claimFailureRetainsIdentity(); }
    { TestHealthyGraphArm test; test.reentrantCancelAfterArm(); }
    { TestHealthyGraphArm test; test.cancelAfterValidationBeforeArm(); }
    { TestHealthyGraphArm test; test.duplicateCancellationRejected(); }
    { TestHealthyGraphArm test; test.changedCoreCannotSettle(); }
    { TestHealthyGraphArm test; test.wrongGateRejected(); }
  }
};

int main(int argc, char** argv)
{
  QCoreApplication application(argc, argv);
  TestHealthyGraphArm::run();
}
