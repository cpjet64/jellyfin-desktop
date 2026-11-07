#include "graph.h"
#include "graph_session.h"
#include "child_session.h"
#include "module_endpoints.h"
#include "RestartWire.h"
#include "player/PlayerComponent.h"
#include "player_test_cache.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace HealthyModuleIdentity;
using namespace HealthyRetirement;
using namespace FixtureRestart;
#define check(value) do { if (!(value)) { std::fprintf(stderr, "check failed at line %d\n", __LINE__); std::_Exit(3); } } while (false)

namespace {
struct PipeScript {
  struct Pending { HANDLE pipe = nullptr; void* bytes = nullptr; DWORD size = 0; OVERLAPPED* overlap = nullptr; };
  QByteArray grant, control;
  std::vector<QByteArray> writes;
  Pending pending[3];
  DWORD error = ERROR_SUCCESS;
  bool endpointValid = true, holdAck = false, releaseAck = false, abortAck = false;
  bool loseOnControlPoll = false, abortControl = false;
};
PipeScript* script = nullptr;
HANDLE WINAPI makeEvent(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR) { return reinterpret_cast<HANDLE>(99); }
BOOL WINAPI yesHandle(HANDLE) { return TRUE; }
ULONGLONG WINAPI ioNow() { return 1; }
DWORD WINAPI ioError() { return script->error; }
BOOL WINAPI ioCancel(HANDLE, LPOVERLAPPED) { return TRUE; }

bool deliver(HANDLE pipe, void* output, DWORD size, DWORD* count)
{
  auto& data = pipe == reinterpret_cast<HANDLE>(101) ? script->grant : script->control;
  if (data.isEmpty()) {
    *count = 0;
    script->error = pipe == reinterpret_cast<HANDLE>(101) ? ERROR_BROKEN_PIPE : ERROR_IO_PENDING;
    return false;
  }
  const auto n = std::min<DWORD>(size, DWORD(data.size()));
  std::memcpy(output, data.constData(), n); data.remove(0, n); *count = n;
  return true;
}
BOOL WINAPI ioRead(HANDLE pipe, LPVOID output, DWORD size, LPDWORD count, LPOVERLAPPED overlap)
{
  if (deliver(pipe, output, size, count)) return TRUE;
  if (script->error == ERROR_IO_PENDING) {
    auto& pending = script->pending[pipe == reinterpret_cast<HANDLE>(101) ? 0 : 1];
    pending = {pipe, output, size, overlap};
  }
  return FALSE;
}
BOOL WINAPI ioResult(HANDLE pipe, LPOVERLAPPED overlap, LPDWORD count, BOOL)
{
  if (pipe == reinterpret_cast<HANDLE>(103)) {
    auto& pending = script->pending[2];
    if (pending.overlap != overlap) { script->error = ERROR_INVALID_HANDLE; return FALSE; }
    if (script->abortAck) { *count = 0; script->error = ERROR_OPERATION_ABORTED; pending = {}; return FALSE; }
    if (!script->releaseAck) { script->error = ERROR_IO_INCOMPLETE; return FALSE; }
    script->writes.emplace_back(static_cast<const char*>(pending.bytes), int(pending.size));
    *count = pending.size; pending = {}; return TRUE;
  }
  auto& pending = script->pending[pipe == reinterpret_cast<HANDLE>(101) ? 0 : 1];
  if (pending.overlap != overlap) { script->error = ERROR_INVALID_HANDLE; return FALSE; }
  if (pipe == reinterpret_cast<HANDLE>(102) && script->abortControl) {
    *count = 0; script->error = ERROR_OPERATION_ABORTED; pending = {}; return FALSE;
  }
  if (pipe == reinterpret_cast<HANDLE>(102) && script->loseOnControlPoll) {
    script->loseOnControlPoll = false; script->endpointValid = false;
  }
  if (!deliver(pipe, pending.bytes, pending.size, count)) {
    if (script->error == ERROR_IO_PENDING) script->error = ERROR_IO_INCOMPLETE;
    return FALSE;
  }
  pending = {};
  return TRUE;
}
BOOL WINAPI ioWrite(HANDLE pipe, LPCVOID input, DWORD size, LPDWORD count, LPOVERLAPPED overlap)
{
  if (script->holdAck && pipe == reinterpret_cast<HANDLE>(103)) {
    script->pending[2] = {pipe, const_cast<void*>(input), size, overlap};
    *count = 0; script->error = ERROR_IO_PENDING; return FALSE;
  }
  script->writes.emplace_back(static_cast<const char*>(input), int(size)); *count = size; return TRUE;
}
bool validModule(void*) noexcept { return true; }

ChildEndpoints::TestOperations endpointOperations()
{
  ChildEndpoints::TestOperations ops;
  ops.valid = [] { return script && script->endpointValid; };
  ops.accepts = [](std::uintptr_t value) { return value != 0; };
  ops.compare = [](std::uintptr_t a, std::uintptr_t b) {
    return a == b ? EndpointComparison::Same : EndpointComparison::Different;
  };
  ops.duplicate = [](std::uintptr_t value, HANDLE& out) { out = reinterpret_cast<HANDLE>(value + 100); return true; };
  ops.close = [](HANDLE) { return true; };
  ops.io.createEvent = makeEvent; ops.io.closeHandle = yesHandle; ops.io.resetEvent = yesHandle;
  ops.io.now = ioNow; ops.io.readFile = ioRead; ops.io.writeFile = ioWrite;
  ops.io.result = ioResult; ops.io.cancel = ioCancel; ops.io.error = ioError;
  return ops;
}
}

class TestHealthyGraphArm {
  static constexpr qint64 normal = 1000000000LL, overall = 31000000000LL;
  struct State {
    qint64 now = 10;
    int stops = 0;
    quint64 idle = 0, position = 0;
    mpv_event noEvent{MPV_EVENT_NONE, 0, 0, nullptr};
  };
  PlayerTestCache cache_;
  PipeScript pipes_;
  std::shared_ptr<State> state_ = std::make_shared<State>();
  Grant grant_{1, 2, 3, QByteArray(64, 'a'), 3000, true};
  QByteArray digest_ = QCryptographicHash::hash(encodeGrant(grant_), QCryptographicHash::Sha256);
  std::unique_ptr<PreparedGraph> prepared_;
  std::shared_ptr<ChildEndpoints> endpoints_;
  FixtureGraph* graph_ = nullptr;
  std::unique_ptr<ChildSession> session_;

  void initialize()
  {
    script = &pipes_; pipes_.grant = encodeGrant(grant_);
    LaunchBinding binding; binding.role = LaunchRole::Restore; binding.run = grant_.run;
    std::memcpy(binding.expectedGrantDigest.data(), digest_.constData(), size_t(digest_.size()));
    auto module = VerifiedModule::forAccessorTest(9, 7, binding, validModule, nullptr);
    EndpointInput inputs[] = {{EndpointRole::Grant, 1}, {EndpointRole::Control, 2}, {EndpointRole::Ack, 3}};
    auto endpoints = ChildEndpoints::createForTest(module, 9, overall, EndpointMode::Restore, inputs, endpointOperations());
    check(endpoints && !module); endpoints_ = std::shared_ptr<ChildEndpoints>(std::move(endpoints));
    const auto state = state_;
    prepared_ = FixtureGraph::prepare(9, normal, overall, [state] { return state->now; });
    check(bool(prepared_)); graph_ = prepared_->graphOnGuiThread(); check(graph_);
    graph_->containment_ = prepared_->containment(); graph_->module_ = endpoints_->capability();
    graph_->endpoints_ = endpoints_; graph_->access_->endpoints = endpoints_;
    graph_->player_ = std::make_unique<PlayerComponent>(); graph_->player_->m_mpvGeneration = 8;
    graph_->player_->m_restoreDispatch = [state](quint64, const char**) { ++state->stops; return 0; };
    graph_->player_->m_healthyStopClock = [state] { return state->now; };
    graph_->player_->m_healthyStopEventWait = [state] { return &state->noEvent; };
    graph_->player_->m_healthyStopDispatch = [state](quint64 id, const char** args, bool query) {
      if (!query) { ++state->stops; return 0; }
      if (!std::strcmp(args[0], "idle-active")) state->idle = id;
      else if (!std::strcmp(args[0], "playlist-pos")) state->position = id;
      else return -1;
      return 0;
    };
    graph_->owner_ = std::make_shared<Owner>(prepared_->containment(), overall, [state] { return state->now; },
                                             [](std::shared_ptr<RenderJob>) { return false; });
    session_ = std::make_unique<ChildSession>(*prepared_, endpoints_, 9);
    check(session_->beginRestore(QString()));
  }

  void replyNoLoad()
  {
    check(state_->idle && state_->position);
    int idle = 1; mpv_event_property idleProperty{"idle-active", MPV_FORMAT_FLAG, &idle};
    mpv_event idleReply{MPV_EVENT_GET_PROPERTY_REPLY, 0, state_->idle, &idleProperty};
    graph_->player_->observeNativeRestoreCleanup(&idleReply); graph_->player_->observeNativeReceipt(&idleReply);
    int64_t position = -1; mpv_event_property positionProperty{"playlist-pos", MPV_FORMAT_INT64, &position};
    mpv_event positionReply{MPV_EVENT_GET_PROPERTY_REPLY, 0, state_->position, &positionProperty};
    graph_->player_->observeNativeRestoreCleanup(&positionReply); graph_->player_->observeNativeReceipt(&positionReply);
  }

  void runCase()
  {
    initialize();
    for (int i = 0; i < 20 && pipes_.writes.empty(); ++i) check(session_->pollRestore() != ChildSession::Progress::Failed);
    check(pipes_.writes.size() == 1 && pipes_.writes.front().left(4) == "JGA2");
    pipes_.control = encodeControl({ControlKind::UserStop, 1, 4});
    for (int i = 0; i < 20 && !graph_->player_->m_restoreCleanup.claimed; ++i)
      check(session_->pollRestore() != ChildSession::Progress::Failed);
    auto& cleanup = graph_->player_->m_restoreCleanup;
    check(cleanup.claimed && cleanup.origin == HealthyRestart::RestoreCleanupState::Origin::AuthenticatedCancel);
    check(cleanup.cancel && cleanup.cancel->sequence == 1 && cleanup.cancel->revision == 4);
    session_->invalidate();
    check(cleanup.origin == HealthyRestart::RestoreCleanupState::Origin::InternalFailure);
    check(cleanup.firstFailure == HealthyRestart::FailureCause::OwnershipLost);
    check(cleanup.cancel && cleanup.cancel->sequence == 1 && cleanup.cancel->revision == 4);
    QByteArray forbidden("unchanged");
    check(!graph_->restorePollCancellation(forbidden) && forbidden.isEmpty());
    (void)graph_->player_->beginNativeRestoreCleanup(graph_->session_->gate, std::nullopt, overall,
                                                      HealthyRestart::FailureCause::ProtocolViolation);
    check(cleanup.firstFailure == HealthyRestart::FailureCause::OwnershipLost);
    const auto pending = session_->pollFailureCleanup();
    if (pending == ChildSession::Progress::Pending) {
      replyNoLoad();
      const auto terminal = session_->pollFailureCleanup();
      check(terminal == ChildSession::Progress::Ready || terminal == ChildSession::Progress::Failed);
    } else {
      check(pending == ChildSession::Progress::Failed);
    }
    check(cleanup.origin == HealthyRestart::RestoreCleanupState::Origin::InternalFailure);
    check(cleanup.firstFailure == HealthyRestart::FailureCause::OwnershipLost);
    check(session_->pollRestore() == ChildSession::Progress::Failed);
    check(pipes_.writes.size() == 1 && !prepared_->closedAck());
    check(!session_->load() && !session_->seek() && state_->stops == 0);
  }
  void endpointLossAfterArm()
  {
    initialize();
    for (int i = 0; i < 20 && pipes_.writes.empty(); ++i) check(session_->pollRestore() != ChildSession::Progress::Failed);
    check(pipes_.writes.size() == 1 && pipes_.writes[0].left(4) == "JGA2");
    check(graph_->player_->m_nativeRestore.armed && !graph_->player_->m_nativeRestore.loadAttempted);
    pipes_.endpointValid = false;
    const auto result = session_->pollRestore();
    if (result != ChildSession::Progress::Failed) std::fprintf(stderr, "endpoint loss result=%d\n", int(result));
    check(result == ChildSession::Progress::Failed);
    check(graph_->session_->gate->canceled());
    check(session_->settleControlRead() == ChildSession::Progress::Pending);
    pipes_.abortControl = true;
    check(session_->settleControlRead() == ChildSession::Progress::Ready);
    check(session_->settleControlRead() == ChildSession::Progress::Ready);
    check(!session_->load() && !session_->seek() && !prepared_->closedAck());
    check(pipes_.writes.size() == 1 && state_->stops == 0);
  }
  void acceptanceWriteInFlightAtFailure()
  {
    pipes_.holdAck = true;
    initialize();
    for (int i = 0; i < 20 && !pipes_.pending[2].overlap; ++i)
      check(session_->pollRestore() != ChildSession::Progress::Failed);
    check(pipes_.pending[2].overlap && pipes_.writes.empty());
    check(graph_->player_->m_nativeRestore.armed && !graph_->player_->m_nativeRestore.loadAttempted);
    const auto gate = graph_->session_->gate;
    const auto claim = std::atomic_load(&graph_->access_->intentClaim);
    check(gate && claim && !gate->canceled());
    prepared_->invalidate();
    check(claim->contained() && !prepared_->graphOnGuiThread());
    session_->invalidate();
    check(graph_->session_->gate == gate && gate->canceled());
    check(graph_->session_->firstFailure == HealthyRestart::FailureCause::OwnershipLost);
    check(std::atomic_load(&graph_->access_->intentClaim) == claim && claim->contained());
    check(session_->pollFailureCleanup() == ChildSession::Progress::Failed);
    check(!prepared_->slot()->hasContextRecord());
    check(session_->settleAckWrite() == ChildSession::Progress::Pending);
    pipes_.releaseAck = true;
    check(session_->settleAckWrite() == ChildSession::Progress::Ready);
    check(!endpoints_->retainedSettlement(EndpointRole::Ack));
    check(session_->settleAckWrite() == ChildSession::Progress::Ready);
    check(!endpoints_->retainedSettlement(EndpointRole::Ack));
    check(pipes_.writes.size() == 1 && pipes_.writes[0].left(4) == "JGA2");
    const auto& binding = *graph_->session_;
    const GrantAccepted expected{grant_.run, 7, grant_.grant, grant_.revision, digest_,
        binding.host, binding.core, binding.intentRevision, binding.attempt};
    GrantAccepted actual;
    check(decodeGrantAccepted(pipes_.writes[0], expected, actual));
    check(actual.host == binding.host && actual.core == binding.core &&
          actual.intentRevision == binding.intentRevision && actual.attempt == binding.attempt);
    check(session_->pollRestore() == ChildSession::Progress::Failed);
    session_->invalidate();
    check(graph_->session_->firstFailure == HealthyRestart::FailureCause::OwnershipLost && claim->contained());
    check(!session_->load() && !session_->seek() && !prepared_->closedAck());
    check(pipes_.writes.size() == 1 && state_->stops == 0);
  }
  void endpointLossDuringPoll()
  {
    initialize();
    for (int i = 0; i < 20 && pipes_.writes.empty(); ++i) check(session_->pollRestore() != ChildSession::Progress::Failed);
    check(pipes_.writes.size() == 1 && graph_->player_->m_nativeRestore.armed);
    pipes_.loseOnControlPoll = true;
    check(session_->pollRestore() == ChildSession::Progress::Failed);
    check(!pipes_.endpointValid && graph_->session_->gate->canceled());
    check(!session_->load() && !session_->seek() && !prepared_->closedAck());
    check(pipes_.writes.size() == 1 && state_->stops == 0);
  }
  void readyThenLossBeforeDirectLoad()
  {
    initialize();
    auto progress = ChildSession::Progress::Pending;
    for (int i = 0; i < 20 && progress == ChildSession::Progress::Pending; ++i)
      progress = session_->pollRestore();
    check(progress == ChildSession::Progress::Ready);
    check(pipes_.writes.size() == 1 && pipes_.writes[0].left(4) == "JGA2");
    const auto& binding = *graph_->session_;
    check(graph_->player_->m_nativeRestore.armed && !graph_->player_->m_nativeRestore.loadAttempted);
    check(binding.host && binding.core && binding.intentRevision && binding.attempt);
    check(!binding.gate->allowsRestore(grant_.run, 7, grant_.grant, grant_.revision, digest_, state_->now));
    pipes_.endpointValid = false;
    check(!session_->load()); // No separate poll after endpoint loss.
    check(binding.gate->canceled() && binding.firstFailure != HealthyRestart::FailureCause::None);
    check(session_->pollRestore() == ChildSession::Progress::Failed);
    check(!graph_->player_->m_nativeRestore.loadAttempted && !session_->seek());
    check(pipes_.writes.size() == 1 && !prepared_->closedAck() && state_->stops == 0);
  }
  void queuedPermitThenEndpointLoss()
  {
    initialize();
    auto progress = ChildSession::Progress::Pending;
    for (int i = 0; i < 20 && progress == ChildSession::Progress::Pending; ++i)
      progress = session_->pollRestore();
    check(progress == ChildSession::Progress::Ready);
    const auto& binding = *graph_->session_;
    check(graph_->player_->m_nativeRestore.armed && !graph_->player_->m_nativeRestore.loadAttempted);
    check(binding.host && binding.core && binding.intentRevision && binding.attempt);
    const auto permit = encodeRestorePermit({grant_.run, 7, grant_.grant, grant_.revision, 1, digest_});
    check(!permit.isEmpty() && pipes_.pending[1].overlap);
    pipes_.control = permit;
    pipes_.endpointValid = false;
    check(session_->pollRestore() == ChildSession::Progress::Failed);
    check(binding.gate->canceled());
    check(!binding.gate->allowsRestore(grant_.run, 7, grant_.grant, grant_.revision, digest_, state_->now));
    check(pipes_.control == permit); // The lost endpoint did not admit its pending read.
    check(!session_->load() && !session_->seek() && !graph_->player_->m_nativeRestore.loadAttempted);
    check(pipes_.writes.size() == 1 && pipes_.writes[0].left(4) == "JGA2");
    check(!prepared_->closedAck() && state_->stops == 0);
  }
  void acceptanceWriteAbortsAfterFailure()
  {
    pipes_.holdAck = true;
    initialize();
    for (int i = 0; i < 20 && !pipes_.pending[2].overlap; ++i)
      check(session_->pollRestore() != ChildSession::Progress::Failed);
    check(pipes_.pending[2].overlap && graph_->player_->m_nativeRestore.armed);
    const auto gate = graph_->session_->gate;
    const auto claim = std::atomic_load(&graph_->access_->intentClaim);
    check(gate && claim && !gate->canceled());
    prepared_->invalidate();
    check(claim->contained() && !prepared_->graphOnGuiThread());
    session_->invalidate();
    check(graph_->session_->gate == gate && gate->canceled());
    check(graph_->session_->firstFailure == HealthyRestart::FailureCause::OwnershipLost);
    check(std::atomic_load(&graph_->access_->intentClaim) == claim && claim->contained());
    check(session_->pollFailureCleanup() == ChildSession::Progress::Failed);
    check(!prepared_->slot()->hasContextRecord());
    check(session_->settleAckWrite() == ChildSession::Progress::Pending);
    pipes_.abortAck = true;
    check(session_->settleAckWrite() == ChildSession::Progress::Ready);
    check(!endpoints_->retainedSettlement(EndpointRole::Ack));
    check(session_->settleAckWrite() == ChildSession::Progress::Ready);
    check(!endpoints_->retainedSettlement(EndpointRole::Ack));
    check(pipes_.writes.empty() && session_->pollRestore() == ChildSession::Progress::Failed);
    session_->invalidate();
    check(graph_->session_->firstFailure == HealthyRestart::FailureCause::OwnershipLost && claim->contained());
    check(!session_->load() && !session_->seek() && !prepared_->closedAck() && state_->stops == 0);
  }
public:
  static bool run(const QString& name)
  {
    TestHealthyGraphArm test;
    if (name == "ready-loss") test.readyThenLossBeforeDirectLoad();
    else if (name == "queued-permit-loss") test.queuedPermitThenEndpointLoss();
    else if (name == "loss-after-arm") test.endpointLossAfterArm();
    else if (name == "loss-during-poll") test.endpointLossDuringPoll();
    else if (name == "initialization-failure") test.runCase();
    else if (name == "ack-in-flight") test.acceptanceWriteInFlightAtFailure();
    else if (name == "ack-aborts") test.acceptanceWriteAbortsAfterFailure();
    else return false;
    return true;
  }
};

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  return argc == 2 && TestHealthyGraphArm::run(QString::fromLocal8Bit(argv[1])) ? 0 : 1;
}
