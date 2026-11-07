#include "graph.h"
#include "graph_session.h"
#include "module_endpoints.h"
#include "RestartWire.h"
#include "FixtureRestartAdapter.h"
#include "RestartChildPlayer.h"
#include "player/PlayerComponent.h"
#include "player_test_cache.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QTemporaryFile>
#include <cstdlib>
#include <cstring>
#include <utility>

using namespace HealthyModuleIdentity;
using namespace HealthyRetirement;
using namespace FixtureRestart;
static void check(bool value) { if (!value) std::abort(); }

class TestRetirementClaim {
public:
  static bool matches(const std::shared_ptr<RetirementClaim>& claim, quint64 host, quint64 core,
                      quint64 revision, quint64 attempt)
  { return claim && claim->matchesArm(9, {host, core, revision, attempt}); }
};

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

ChildEndpoints::TestOperations endpointOperations(std::function<bool()> valid)
{
  ChildEndpoints::TestOperations ops;
  ops.valid = std::move(valid);
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
    bool endpointLive = true, failCleanupCheck = false;
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
    if (state->failCleanupCheck && state->graph && state->graph->session_ && state->graph->session_->host) {
      state->endpointLive = false; state->failCleanupCheck = false;
    }
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

  void initialize(const QString& path = {})
  {
    LaunchBinding binding;
    binding.role = LaunchRole::Restore; binding.run = grant_.run;
    std::memcpy(binding.expectedGrantDigest.data(), digest_.constData(), size_t(digest_.size()));
    auto module = VerifiedModule::forAccessorTest(9, 7, binding, validModule, nullptr);
    EndpointInput inputs[] = {{EndpointRole::Grant, 1}, {EndpointRole::Control, 2}, {EndpointRole::Ack, 3}};
    const auto state = state_;
    auto endpoints = ChildEndpoints::createForTest(module, 9, overall, EndpointMode::Restore, inputs,
                                                    endpointOperations([state] { return state->endpointLive; }));
    check(endpoints && !module);
    auto shared = std::shared_ptr<ChildEndpoints>(std::move(endpoints));
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
    check(graph_->restoreInitialize(path));
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
  QByteArray permit() const
  { return encodeRestorePermit({grant_.run, 7, grant_.grant, grant_.revision, 1, digest_}); }
  bool childPermit(QByteArray& response)
  {
    return graph_->session_->child->control(graph_->endpoints_->token(EndpointRole::Control), permit(), response);
  }
  bool gateAllowsRestore() const
  { return graph_->session_->gate->allowsRestore(grant_.run, 7, grant_.grant, grant_.revision, digest_, state_->now); }
  void initializeWithFile(QTemporaryFile& file)
  {
    check(file.open() && file.write("fixture", 7) == 7 && file.flush());
    file.close();
    grant_.assetHash = QCryptographicHash::hash(QByteArray("fixture"), QCryptographicHash::Sha256).toHex();
    digest_ = QCryptographicHash::hash(encodeGrant(grant_), QCryptographicHash::Sha256);
    initialize(file.fileName());
  }
  void normalPermitBaseline()
  {
    QTemporaryFile file(QDir(QString::fromLocal8Bit(qgetenv("JELLYFIN_TEST_OWNED_ROOT"))).filePath("arm-fixture-XXXXXX"));
    initializeWithFile(file); QByteArray response;
    check(graph_->restoreGrant(encodeGrant(grant_), true, response));
    response.clear(); check(childPermit(response) && gateAllowsRestore());
    check(graph_->session_->child->load() && state_->dispatches == 1);
  }
  void failedCleanupReadinessRejectsPermit()
  {
    QTemporaryFile file(QDir(QString::fromLocal8Bit(qgetenv("JELLYFIN_TEST_OWNED_ROOT"))).filePath("arm-fixture-XXXXXX"));
    initializeWithFile(file);
    const void* control = graph_->endpoints_->token(EndpointRole::Control);
    check(control); state_->failCleanupCheck = true; QByteArray response;
    check(!graph_->restoreGrant(encodeGrant(grant_), true, response));
    check(!state_->failCleanupCheck && !state_->endpointLive && response.isEmpty());
    const auto key = graph_->player_->m_nativeRestore.key;
    const auto& s = *graph_->session_;
    check(key.host && key.core && key.revision && key.attempt && s.host == key.host &&
          s.core == key.core && s.intentRevision == key.revision && s.attempt == key.attempt);
    check(s.gate->canceled());
    check(!s.child->control(control, permit(), response) && response.isEmpty() && !gateAllowsRestore());
    check(!s.child->load() && !s.adapter->load(file.fileName()) && state_->dispatches == 0);
  }
  void playerBindingFailureRetainsUnknown()
  {
    initialize(); bool hookCalled = false; PlaybackNativeReceipts::Key actual;
    setGraphBindHookForTest([&] {
      hookCalled = true; actual = graph_->player_->m_nativeRestore.key;
      check(actual.host && actual.core && actual.revision && actual.attempt);
      graph_->player_->revokeNativeReceiptHost();
    });
    QByteArray response;
    const bool granted = graph_->restoreGrant(encodeGrant(grant_), true, response);
    setGraphBindHookForTest({});
    check(hookCalled && !granted && response.isEmpty() && !graph_->player_->m_healthyStopLease);
    const auto& s = *graph_->session_;
    const auto claim = std::atomic_load(&graph_->access_->intentClaim);
    check(s.host == actual.host && s.core == actual.core && s.intentRevision == actual.revision &&
          s.attempt == actual.attempt && TestRetirementClaim::matches(claim, actual.host, actual.core,
                                                                     actual.revision, actual.attempt));
    check(claim && !claim->eligible() && !prepared_->closedAck() && state_->dispatches == 0);
    check(!prepared_->beginRestoreFailure(HealthyRestart::FailureCause::OwnershipLost));
    check(prepared_->pollRestoreFailure() == PreparedGraph::FailureProgress::Unknown);
    check(state_->dispatches == 0 && !graph_->restoreLoad() && !graph_->restoreSeek());
  }
  void replyNoLoad()
  {
    check(state_->idleQuery && state_->positionQuery);
    int idle = 1; mpv_event_property idleProperty{"idle-active", MPV_FORMAT_FLAG, &idle};
    mpv_event idleReply{MPV_EVENT_GET_PROPERTY_REPLY, 0, state_->idleQuery, &idleProperty};
    graph_->player_->observeNativeRestoreCleanup(&idleReply);
    graph_->player_->observeNativeReceipt(&idleReply);
    int64_t position = -1; mpv_event_property positionProperty{"playlist-pos", MPV_FORMAT_INT64, &position};
    mpv_event positionReply{MPV_EVENT_GET_PROPERTY_REPLY, 0, state_->positionQuery, &positionProperty};
    graph_->player_->observeNativeRestoreCleanup(&positionReply);
    graph_->player_->observeNativeReceipt(&positionReply);
  }
  void settleNoLoad(QByteArray& response)
  {
    if (!state_->idleQuery || !state_->positionQuery) check(graph_->restorePollCancellation(response));
    check(response.isEmpty());
    replyNoLoad();
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
    const auto claim = std::atomic_load(&graph_->access_->intentClaim);
    check(claim && claim == graph_->player_->m_healthyStopLease && claim->eligible());
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
  void foreignClaimPreventsArm()
  {
    initialize();
    std::atomic_store(&graph_->access_->intentClaim, std::make_shared<RetirementClaim>(9));
    QByteArray response;
    check(!graph_->restoreGrant(encodeGrant(grant_), true, response));
    const auto& s = *graph_->session_;
    check(response.isEmpty() && !s.host && !s.core && !s.intentRevision && !s.attempt);
    check(!graph_->player_->m_nativeRestore.armed && !prepared_->closedAck());
    check(!graph_->restoreLoad() && !graph_->restoreSeek() && state_->dispatches == 0);
  }
  void failureCrossesInstallation()
  {
    initialize(); bool hookCalled = false, failureStarted = false;
    setGraphInstallHookForTest([&] {
      hookCalled = true;
      failureStarted = prepared_->beginRestoreFailure(HealthyRestart::FailureCause::ProtocolViolation);
      return false;
    });
    QByteArray response;
    const bool grantSucceeded = graph_->restoreGrant(encodeGrant(grant_), true, response);
    setGraphInstallHookForTest({});
    check(hookCalled && failureStarted && !grantSucceeded && response.isEmpty());
    const auto key = graph_->player_->m_nativeRestore.key;
    const auto& s = *graph_->session_;
    check(key.host && key.core == 8 && key.revision && key.attempt && s.host == key.host &&
          s.core == key.core && s.intentRevision == key.revision && s.attempt == key.attempt);
    check(prepared_->pollRestoreFailure() == PreparedGraph::FailureProgress::Pending);
    replyNoLoad();
    check(prepared_->pollRestoreFailure() == PreparedGraph::FailureProgress::Qualified);
    const auto claim = std::atomic_load(&graph_->access_->intentClaim);
    check(claim && !claim->eligible() && !claim->open() && !prepared_->closedAck());
    check(!graph_->restoreLoad() && !graph_->restoreSeek());
    check(state_->dispatches == 0);
  }
  void installationFailureRetainsCleanupAuthority()
  {
    initialize(); bool hookCalled = false;
    setGraphInstallHookForTest([&] { hookCalled = true; return false; });
    QByteArray response;
    const bool grantSucceeded = graph_->restoreGrant(encodeGrant(grant_), true, response);
    setGraphInstallHookForTest({});
    check(hookCalled && !grantSucceeded && response.isEmpty());
    const auto key = graph_->player_->m_nativeRestore.key;
    const auto& s = *graph_->session_;
    check(key.host && key.core == 8 && key.revision && key.attempt && s.host == key.host &&
          s.core == key.core && s.intentRevision == key.revision && s.attempt == key.attempt);
    check(prepared_->beginRestoreFailure(HealthyRestart::FailureCause::InitializationFailure));
    check(prepared_->pollRestoreFailure() == PreparedGraph::FailureProgress::Pending);
    replyNoLoad();
    check(prepared_->pollRestoreFailure() == PreparedGraph::FailureProgress::Qualified);
    const auto claim = std::atomic_load(&graph_->access_->intentClaim);
    check(claim && !claim->eligible() && !claim->open() && !prepared_->closedAck());
    check(!graph_->restoreLoad() && !graph_->restoreSeek());
    check(state_->dispatches == 0);
  }
  void reservationPrecedesPlayerArm()
  {
    initialize(); bool hookCalled = false;
    std::shared_ptr<RetirementClaim> reserved;
    setGraphPreArmHookForTest([&] {
      hookCalled = true;
      reserved = std::atomic_load(&graph_->access_->intentClaim);
      check(reserved && !reserved->eligible() && !reserved->open());
      check(!graph_->player_->m_nativeRestore.armed && !graph_->session_->host);
      check(!prepared_->closedAck() && state_->dispatches == 0);
    });
    QByteArray response;
    const bool grantSucceeded = graph_->restoreGrant(encodeGrant(grant_), true, response);
    setGraphPreArmHookForTest({});
    check(hookCalled && grantSucceeded);
    GrantAccepted accepted;
    check(decodeGrantAccepted(response, expectedAcceptance(), accepted));
    const auto key = graph_->player_->m_nativeRestore.key;
    check(accepted.host == key.host && accepted.core == key.core &&
          accepted.intentRevision == key.revision && accepted.attempt == key.attempt);
    check(reserved == std::atomic_load(&graph_->access_->intentClaim) &&
          reserved == graph_->player_->m_healthyStopLease && reserved->eligible());
    check(!graph_->restoreLoad() && !graph_->restoreSeek() && state_->dispatches == 0);
  }
  void containmentCrossesInstallation()
  {
    initialize(); bool hookCalled = false;
    setGraphInstallHookForTest([&] { hookCalled = true; prepared_->invalidate(); return false; });
    QByteArray response;
    const bool grantSucceeded = graph_->restoreGrant(encodeGrant(grant_), true, response);
    setGraphInstallHookForTest({});
    check(hookCalled && !grantSucceeded && response.isEmpty());
    const auto key = graph_->player_->m_nativeRestore.key;
    const auto& s = *graph_->session_;
    check(key.host && key.core == 8 && key.revision && key.attempt && s.host == key.host &&
          s.core == key.core && s.intentRevision == key.revision && s.attempt == key.attempt);
    check(prepared_->pollRestoreFailure() == PreparedGraph::FailureProgress::Unknown);
    const auto claim = std::atomic_load(&graph_->access_->intentClaim);
    check(claim && !claim->eligible() && !prepared_->closedAck() && state_->dispatches == 0);
  }
  void containmentDuringPlayerArm()
  {
    initialize(); bool hookCalled = false;
    setFixtureArmHookForTest([&] {
      hookCalled = true;
      const auto claim = std::atomic_load(&graph_->access_->intentClaim);
      check(graph_->session_->armInFlight && claim && !claim->eligible() && !claim->open());
      check(!graph_->session_->host && graph_->player_->m_nativeRestore.armed);
      check(state_->dispatches == 0 && !prepared_->closedAck());
      prepared_->invalidate();
    });
    QByteArray response;
    const bool grantSucceeded = graph_->restoreGrant(encodeGrant(grant_), true, response);
    setFixtureArmHookForTest({});
    check(hookCalled && !grantSucceeded && response.isEmpty());
    const auto key = graph_->player_->m_nativeRestore.key;
    const auto& s = *graph_->session_;
    check(key.host && key.core == 8 && key.revision && key.attempt && s.host == key.host &&
          s.core == key.core && s.intentRevision == key.revision && s.attempt == key.attempt);
    check(prepared_->pollRestoreFailure() == PreparedGraph::FailureProgress::Unknown);
    const auto claim = std::atomic_load(&graph_->access_->intentClaim);
    check(claim && claim->contained() && !claim->eligible() &&
          TestRetirementClaim::matches(claim, key.host, key.core, key.revision, key.attempt));
    check(!prepared_->closedAck() && state_->dispatches == 0);
  }
  void reentrantCancelAfterArm()
  {
    QTemporaryFile file(QDir(QString::fromLocal8Bit(qgetenv("JELLYFIN_TEST_OWNED_ROOT"))).filePath("arm-fixture-XXXXXX"));
    initializeWithFile(file); state_->cancelOnArm = true; QByteArray response;
    check(graph_->restoreGrant(encodeGrant(grant_), true, response) && state_->cancelCalled);
    GrantAccepted accepted;
    check(decodeGrantAccepted(response, expectedAcceptance(), accepted));
    const auto key = graph_->player_->m_nativeRestore.key;
    check(accepted.host == key.host && accepted.core == key.core &&
          accepted.intentRevision == key.revision && accepted.attempt == key.attempt);
    QByteArray permitResponse;
    check(!childPermit(permitResponse) && permitResponse.isEmpty() && !gateAllowsRestore());
    check(!graph_->session_->child->load() && !graph_->session_->adapter->load(file.fileName()) && state_->dispatches == 0);
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
    check(!graph_->player_->beginNativeRestoreCleanup(
        wrong, std::nullopt, overall, HealthyRestart::FailureCause::ProtocolViolation));
    check(state_->dispatches == 0);
    check(!graph_->restoreLoad() && !graph_->restoreSeek() && state_->dispatches == 0);
  }
  void internalFailureAfterArm()
  {
    initialize(); QByteArray response;
    check(graph_->restoreGrant(encodeGrant(grant_), true, response));
    GrantAccepted accepted;
    check(decodeGrantAccepted(response, expectedAcceptance(), accepted));
    check(prepared_->beginRestoreFailure(HealthyRestart::FailureCause::ProtocolViolation));
    check(prepared_->pollRestoreFailure() == PreparedGraph::FailureProgress::Pending);
    replyNoLoad();
    check(prepared_->pollRestoreFailure() == PreparedGraph::FailureProgress::Qualified);
    check(!prepared_->closedAck());
    check(!graph_->restoreLoad() && !graph_->restoreSeek() && state_->dispatches == 0);
    response.clear();
    check(!graph_->restoreControl(encodeControl({ControlKind::UserStop, 1, 4}), response));
    check(response.isEmpty() && state_->dispatches == 0);
  }
  void containmentBeforeArm()
  {
    initialize(); prepared_->invalidate(); QByteArray response;
    check(!graph_->restoreGrant(encodeGrant(grant_), true, response));
    check(response.isEmpty() && !prepared_->closedAck() && state_->dispatches == 0);
    check(!graph_->restoreLoad() && !graph_->restoreSeek());
  }
  void containmentAfterArm()
  {
    initialize(); QByteArray response;
    check(graph_->restoreGrant(encodeGrant(grant_), true, response));
    const auto key = graph_->player_->m_nativeRestore.key;
    prepared_->invalidate(); response.clear();
    check(!prepared_->beginRestoreFailure(HealthyRestart::FailureCause::OwnershipLost));
    check(prepared_->pollRestoreFailure() == PreparedGraph::FailureProgress::Unknown);
    check(!graph_->restoreControl(encodeControl({ControlKind::UserStop, 1, 4}), response));
    const auto retained = graph_->player_->m_nativeRestore.key;
    check(response.isEmpty() && retained.host == key.host && retained.core == key.core &&
          retained.revision == key.revision && retained.attempt == key.attempt);
    check(!prepared_->closedAck() && state_->dispatches == 0);
  }
public:
  static bool run(const QByteArray& selected)
  {
#define RUN_CASE(name) if (selected == #name) { TestHealthyGraphArm test; test.name(); return true; }
    RUN_CASE(cancelBeforeGrant)
    RUN_CASE(normalPermitBaseline)
    RUN_CASE(failedCleanupReadinessRejectsPermit)
    RUN_CASE(playerBindingFailureRetainsUnknown)
    RUN_CASE(deadlineAfterArm)
    RUN_CASE(foreignClaimPreventsArm)
    RUN_CASE(failureCrossesInstallation)
    RUN_CASE(installationFailureRetainsCleanupAuthority)
    RUN_CASE(reservationPrecedesPlayerArm)
    RUN_CASE(containmentCrossesInstallation)
    RUN_CASE(containmentDuringPlayerArm)
    RUN_CASE(reentrantCancelAfterArm)
    RUN_CASE(cancelAfterValidationBeforeArm)
    RUN_CASE(duplicateCancellationRejected)
    RUN_CASE(changedCoreCannotSettle)
    RUN_CASE(wrongGateRejected)
    RUN_CASE(internalFailureAfterArm)
    RUN_CASE(containmentBeforeArm)
    RUN_CASE(containmentAfterArm)
#undef RUN_CASE
    return false;
  }
};
int main(int argc, char** argv)
{
  QCoreApplication application(argc, argv);
  return argc == 2 && TestHealthyGraphArm::run(argv[1]) ? 0 : 2;
}
