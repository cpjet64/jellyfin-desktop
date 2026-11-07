#include "graph_session.h"
#include "fixture_restart/FixtureRestartAdapter.h"
#include "fixture_restart/RestartChildPlayer.h"
#include "fixture_restart/RestartSourcePlayer.h"
#include "module_endpoints.h"
#include "player/PlayerComponent.h"
#include <QRandomGenerator>
#include <QScopedValueRollback>
#include <QThread>

namespace HealthyRetirement {
#ifdef HEALTHY_RETIREMENT_TESTING
namespace { std::function<void()> preArmHook, bindHook; }
void setGraphPreArmHookForTest(std::function<void()> hook) { preArmHook = std::move(hook); }
void setGraphBindHookForTest(std::function<void()> hook) { bindHook = std::move(hook); }
#endif
namespace {
quint64 freshId()
{
  const auto value = QRandomGenerator::system()->generate64() & PlaybackIntent::MaxSafeInteger;
  return value ? value : 1;
}
QByteArray digestHex(const HealthyModuleIdentity::Digest& value)
{
  return QByteArray(reinterpret_cast<const char*>(value.data()), int(value.size())).toHex();
}
bool closeFailedGate(void*) { return true; }
}
FixtureGraph::Session::Session() = default;
FixtureGraph::Session::~Session() = default;

bool FixtureGraph::sessionReady(bool source) const
{
  const auto containment = containment_.lock();
  if (!dispatcher_ || dispatcher_->thread() != QThread::currentThread() || !containment ||
      !containment->intact() || access_->phase.load() != AccessPhase::Normal ||
      !player_ || retirementRequested_) return false;
  HealthyModuleIdentity::LaunchBinding binding;
  try {
    const auto now = clock_();
    if (now < lastSessionNow_ || now >= normalDeadline_) return false;
    lastSessionNow_ = now;
#ifdef HEALTHY_RETIREMENT_TESTING
    if (!source && restoreBootstrapForTest_) {
      std::uint64_t launch = 0;
      if (!restoreLaunch(binding, launch) || binding.role != HealthyModuleIdentity::LaunchRole::Restore ||
          !binding.run || !launch) return false;
      const QByteArray digest(reinterpret_cast<const char*>(binding.expectedGrantDigest.data()),
                              int(binding.expectedGrantDigest.size()));
      return (!session_ || (binding.run == session_->run && launch == session_->launch && digest == session_->digest)) &&
          restoreToken(HealthyModuleIdentity::EndpointRole::Control) &&
          restoreToken(HealthyModuleIdentity::EndpointRole::Ack) && containment->intact() &&
          access_->phase.load() == AccessPhase::Normal && !access_->revoked.load();
    }
#endif
    if (!module_ || !endpoints_) return false;
    return access_->phase.load() == AccessPhase::Normal && containment->intact() &&
        module_->stillValid(slot_->generation()) &&
        module_->launchBinding(slot_->generation(), binding) &&
        binding.role == (source ? HealthyModuleIdentity::LaunchRole::Source : HealthyModuleIdentity::LaunchRole::Restore) &&
        endpoints_->token(HealthyModuleIdentity::EndpointRole::Control) &&
        endpoints_->token(HealthyModuleIdentity::EndpointRole::Ack);
  } catch (...) { return false; }
}

bool FixtureGraph::cleanupSessionReady() const
{
  const auto containment = containment_.lock();
  if (!dispatcher_ || dispatcher_->thread() != QThread::currentThread() || !containment || !containment->intact() ||
      access_->phase.load() != AccessPhase::Normal || access_->revoked.load() ||
      retirementRequested_) return false;
  try {
    HealthyModuleIdentity::LaunchBinding binding;
    const auto now = clock_();
#ifdef HEALTHY_RETIREMENT_TESTING
    if (restoreBootstrapForTest_) {
      std::uint64_t launch = 0;
      if (now < lastSessionNow_ || now >= deadline_ || !restoreLaunch(binding, launch) ||
          binding.role != HealthyModuleIdentity::LaunchRole::Restore || !binding.run || !launch ||
          !restoreToken(HealthyModuleIdentity::EndpointRole::Control) ||
          !restoreToken(HealthyModuleIdentity::EndpointRole::Ack) || !containment->intact() ||
          access_->revoked.load() || access_->phase.load() != AccessPhase::Normal) return false;
      const QByteArray digest(reinterpret_cast<const char*>(binding.expectedGrantDigest.data()),
                              int(binding.expectedGrantDigest.size()));
      if (session_ && (binding.run != session_->run || launch != session_->launch || digest != session_->digest))
        return false;
      lastSessionNow_ = now;
      return true;
    }
#endif
    if (!module_ || !endpoints_) return false;
    if (now < lastSessionNow_ || now >= deadline_ || !containment->intact() || access_->revoked.load() ||
        access_->phase.load() != AccessPhase::Normal || !module_->stillValid(slot_->generation()) ||
        !module_->launchBinding(slot_->generation(), binding) ||
        binding.role != HealthyModuleIdentity::LaunchRole::Restore ||
        !endpoints_->token(HealthyModuleIdentity::EndpointRole::Control) ||
        !endpoints_->token(HealthyModuleIdentity::EndpointRole::Ack)) return false;
    lastSessionNow_ = now;
    return true;
  } catch (...) { return false; }
}

bool FixtureGraph::failureCleanupReady() const
{
  const auto containment = containment_.lock();
  if (!dispatcher_ || dispatcher_->thread() != QThread::currentThread() || !containment || !containment->intact() ||
      !player_ || !session_ || !session_->gate || access_->phase.load() != AccessPhase::CleanupOnly) return false;
  try {
    const auto now = clock_();
#ifdef HEALTHY_RETIREMENT_TESTING
    if (restoreBootstrapForTest_) {
      HealthyModuleIdentity::LaunchBinding binding;
      std::uint64_t launch = 0;
      if (now < lastSessionNow_ || now >= deadline_ || !restoreLaunch(binding, launch) ||
          binding.role != HealthyModuleIdentity::LaunchRole::Restore || binding.run != session_->run ||
          launch != session_->launch || !containment->intact() || access_->revoked.load() ||
          access_->phase.load() != AccessPhase::CleanupOnly) return false;
      const QByteArray digest(reinterpret_cast<const char*>(binding.expectedGrantDigest.data()),
                              int(binding.expectedGrantDigest.size()));
      if (digest != session_->digest) return false;
      lastSessionNow_ = now;
      return true;
    }
#endif
    if (!module_) return false;
    if (now < lastSessionNow_ || now >= deadline_ || !containment->intact() || access_->revoked.load() ||
        access_->phase.load() != AccessPhase::CleanupOnly ||
        !module_->stillValid(slot_->generation())) return false;
    lastSessionNow_ = now;
    return true;
  } catch (...) { return false; }
}

bool FixtureGraph::sourceLoad(const QString& path, bool paused)
{
  using namespace HealthyModuleIdentity;
  if (!sessionReady(true) || session_ || !endpoints_->token(EndpointRole::Evidence)) return false;
  LaunchBinding launch;
  if (!module_->launchBinding(slot_->generation(), launch)) return false;
  const auto intent = player_->m_playbackIntent.snapshot();
  if (!launch.revision || intent.revision == PlaybackIntent::MaxSafeInteger || intent.revision + 1 != launch.revision)
    return false;
  try {
    session_ = std::make_unique<Session>();
    session_->binding.run = launch.run;
    session_->binding.revision = launch.revision;
    session_->binding.assetHash = digestHex(launch.assetDigest);
    session_->gate = std::make_shared<FixtureRestart::ChildGate>(nullptr, normalDeadline_);
    session_->adapter = std::make_unique<FixtureRestartAdapter>(*player_, *session_->gate, freshId(), freshId());
    if (!session_->adapter->loadA(path, session_->binding.assetHash, paused) ||
        player_->m_playbackIntent.snapshot().revision != launch.revision || !installRetirementClaim()) {
      invalidate(); return false;
    }
    session_->source = std::make_unique<FixtureRestart::RestartSourcePlayer>(*session_->adapter,
        endpoints_->token(EndpointRole::Evidence), endpoints_->token(EndpointRole::Control), session_->binding);
    if (sessionReady(true)) return true;
    invalidate(); return false;
  } catch (...) { invalidate(); return false; }
}

bool FixtureGraph::sourceFreeze(QByteArray& frame)
{
  frame.clear();
  if (!sessionReady(true) || !session_ || !session_->source) return false;
  try {
    if (session_->source->freeze(endpoints_->token(HealthyModuleIdentity::EndpointRole::Evidence), frame) &&
        sessionReady(true)) return true;
  } catch (...) {}
  frame.clear(); invalidate(); return false;
}
bool FixtureGraph::sourceBeginStop(const QByteArray& control)
{
  if (!sessionReady(true) || !session_ || !session_->source) return false;
  try {
    if (session_->source->beginControl(endpoints_->token(HealthyModuleIdentity::EndpointRole::Control), control, normalDeadline_) &&
        sessionReady(true)) return true;
  } catch (...) {}
  invalidate(); return false;
}
HealthyRestart::AsyncStop::Status FixtureGraph::sourcePollStop(QByteArray& response)
{
  response.clear();
  if (!sessionReady(true) || !session_ || !session_->source) return HealthyRestart::AsyncStop::Status::Failed;
  try {
    const auto result = session_->source->pollControl(endpoints_->token(HealthyModuleIdentity::EndpointRole::Control), response);
    if (sessionReady(true) && result != HealthyRestart::AsyncStop::Status::Failed) return result;
  } catch (...) {}
  response.clear(); invalidate(); return HealthyRestart::AsyncStop::Status::Failed;
}

FixtureRestart::RestartChild::ArmResult FixtureGraph::restoreArmActual()
{
  using Result = FixtureRestart::RestartChild::ArmResult;
  const auto* grant = session_->gate->acceptedGrant();
  if (!grant || !sessionReady(false))
    return session_->gate->canceled() ? session_->adapter->armActual() : Result{};
  const auto binding = *grant;
  session_->binding = binding;
  auto claim = RetirementClaim::makeArmReservation(slot_->generation());
  if (!claim || !claim->reserveArm(slot_->generation()) ||
      access_->phase.load() != AccessPhase::Normal || access_->revoked.load()) return Result{};
  std::shared_ptr<RetirementClaim> empty;
  if (!std::atomic_compare_exchange_strong(&access_->intentClaim, &empty, claim)) return Result{};
  if (access_->phase.load() != AccessPhase::Normal || access_->revoked.load()) {
    claim->contain();
    return Result{};
  }
#ifdef HEALTHY_RETIREMENT_TESTING
  try { if (preArmHook) preArmHook(); } catch (...) { claim->contain(); return Result{}; }
#endif
  if (access_->phase.load() != AccessPhase::Normal || access_->revoked.load()) {
    claim->contain();
    return Result{};
  }
  Result result;
  {
    QScopedValueRollback<bool> inFlight(session_->armInFlight, true);
    result = session_->adapter->armActual();
  }
  if (result.status != Result::Status::Armed) return result;
  session_->host = result.identity.host; session_->core = result.identity.core;
  session_->intentRevision = result.identity.intentRevision; session_->attempt = result.identity.attempt;
  const RetirementClaim::ArmIdentity identity{result.identity.host, result.identity.core,
      result.identity.intentRevision, result.identity.attempt};
  if (!claim->bindArm(slot_->generation(), identity)) return Result{};
#ifdef HEALTHY_RETIREMENT_TESTING
  try { if (bindHook) bindHook(); } catch (...) { claim->denyRetirement(slot_->generation(), identity); return Result{}; }
#endif
  if (claim->contained() || !player_->healthyInstallRetirementClaim(claim, slot_->generation(),
      HealthyRestart::ClaimInstallMode::BindRestoreReservation)) {
    claim->denyRetirement(slot_->generation(), identity);
    return Result{};
  }
  if (session_->firstFailure != HealthyRestart::FailureCause::None) {
    beginRestoreFailure(session_->firstFailure);
    return Result{};
  }
  const bool claimInstalled = installRetirementClaim();
  if (!claimInstalled) return Result{}; // Actual identity remains in Session for containment.
  const bool retained = cleanupSessionReady();
  if (session_->firstFailure != HealthyRestart::FailureCause::None) {
    beginRestoreFailure(session_->firstFailure);
    return Result{};
  }
  if (!retained) {
    if (session_->gate->matchesVerifiedCancel(session_->cancelSequence, session_->cancelRevision)) {
      player_->m_healthyStopLease->enterCleanup(slot_->generation());
      return result; // Preserve authenticated cancellation's actual arm receipt.
    }
    session_->firstFailure = HealthyRestart::FailureCause::OwnershipLost;
    session_->gate->cancel(closeFailedGate, nullptr);
    return Result{};
  }
  if (session_->gate->canceled()) player_->m_healthyStopLease->enterCleanup(slot_->generation());
  return result;
}

FixtureRestart::RestartChild::Actions FixtureGraph::restoreActions(const QString& path)
{
  FixtureRestart::RestartChild::Actions actions;
  actions.now = clock_;
  actions.armActual = [this] { return restoreArmActual(); };
  actions.beginCleanup = [this](quint64 sequence, quint64 revision) {
    if (!session_ || !session_->gate || !session_->gate->canceled() || !player_ ||
        !sequence || revision <= session_->binding.revision || !cleanupSessionReady()) return false;
    return session_->adapter && session_->adapter->beginCleanup(sequence, revision, deadline_);
  };
  actions.pollCleanup = [this] {
    using Result = FixtureRestart::RestartChild::CleanupResult;
    Result result;
    if (!session_ || !player_) { result.reason = FixtureRestart::CancelFailure::CleanupUnconfirmed; return result; }
    result.identity = {session_->host, session_->core, session_->intentRevision, session_->attempt};
    if (!cleanupSessionReady()) {
      result.reason = FixtureRestart::CancelFailure::CleanupUnconfirmed; return result;
    }
    if (!session_->adapter) { result.reason = FixtureRestart::CancelFailure::CleanupUnconfirmed; return result; }
    result = session_->adapter->pollCleanup();
    if (!cleanupSessionReady()) {
      result.status = Result::Status::Failed;
      result.reason = FixtureRestart::CancelFailure::CleanupUnconfirmed;
      return result;
    }
    return result;
  };
  actions.load = [this, path] {
    return prepareRestoreAuthority() && session_->adapter->load(path) && sessionReady(false);
  };
  actions.seek = [this] {
    return captureTargetReady() && sessionReady(false) && session_->adapter->seek() && sessionReady(false);
  };
  return actions;
}

bool FixtureGraph::restoreInitialize(const QString& path, std::shared_ptr<FixtureRestart::ChildGate>* retainedGate)
{
  using namespace HealthyModuleIdentity;
  if (!sessionReady(false) || session_) return false;
  const auto grantChannel = restoreToken(EndpointRole::Grant);
  if (!grantChannel) return false;
  try {
    LaunchBinding launch;
    std::uint64_t launchId = 0;
    if (!restoreLaunch(launch, launchId) || launch.role != LaunchRole::Restore || !launch.run || !launchId)
      { invalidate(); return false; }
    const QByteArray digest(reinterpret_cast<const char*>(launch.expectedGrantDigest.data()),
                            int(launch.expectedGrantDigest.size()));
    session_ = std::make_unique<Session>();
    session_->run = launch.run; session_->launch = launchId; session_->digest = digest;
    session_->gate = std::make_shared<FixtureRestart::ChildGate>(grantChannel, normalDeadline_);
    if (retainedGate) *retainedGate = session_->gate;
    session_->adapter = std::make_unique<FixtureRestartAdapter>(*player_, *session_->gate, freshId(), freshId(),
                                                                launchId, digest, clock_);
    if (!session_->adapter->bindCleanupGate(session_->gate, deadline_)) { invalidate(); return false; }
    auto actions = restoreActions(path);
    session_->child = std::make_unique<FixtureRestart::RestartChildPlayer>(*session_->gate, grantChannel,
        restoreToken(EndpointRole::Control), launch.run, launchId, digest, std::move(actions));
    if (sessionReady(false)) return true;
  } catch (...) {}
  invalidate();
  return false;
}
bool FixtureGraph::restoreGrant(const QByteArray& frame, bool eof, QByteArray& response)
{
  response.clear();
  if (!sessionReady(false)) return false;
  if (!session_ || !session_->child) return false;
  try {
    if (session_->child->grant(restoreToken(HealthyModuleIdentity::EndpointRole::Grant),
                               frame, clock_(), eof, response)) {
      if (session_->firstFailure != HealthyRestart::FailureCause::None) { response.clear(); return false; }
      const auto now = clock_();
      FixtureRestart::GrantAccepted accepted;
      const FixtureRestart::GrantAccepted expected{session_->run, session_->launch, session_->binding.grant,
          session_->binding.revision, session_->digest, session_->host, session_->core,
          session_->intentRevision, session_->attempt};
      const bool armed = session_->host && session_->core && session_->intentRevision && session_->attempt &&
          FixtureRestart::decodeGrantAccepted(response, expected, accepted) && accepted.host == session_->host &&
          accepted.core == session_->core && accepted.intentRevision == session_->intentRevision &&
          accepted.attempt == session_->attempt;
      const FixtureRestart::CancelNoArm noArm{session_->run, session_->launch, session_->cancelRevision,
                                             session_->cancelSequence, session_->digest};
      const bool unarmed = session_->gate->canceled() && !session_->host && !session_->core &&
          !session_->intentRevision && !session_->attempt && !player_->m_nativeRestore.armed &&
          !player_->m_nativeRestore.loadAttempted && !player_->m_nativeReceipts.snapshot().key.attempt &&
          !player_->m_lastReceiptAttempt &&
          FixtureRestart::decodeCancelNoArm(response, noArm);
      HealthyModuleIdentity::LaunchBinding liveBinding;
      std::uint64_t liveLaunch = 0;
      if ((armed || unarmed) && access_->phase.load() == AccessPhase::Normal && !access_->revoked.load() &&
          now >= lastSessionNow_ && now < deadline_ && restoreLaunch(liveBinding, liveLaunch) &&
          liveBinding.role == HealthyModuleIdentity::LaunchRole::Restore && liveBinding.run == session_->run &&
          liveLaunch == session_->launch &&
          restoreToken(HealthyModuleIdentity::EndpointRole::Grant) &&
          restoreToken(HealthyModuleIdentity::EndpointRole::Ack)) {
        lastSessionNow_ = now;
        return true;
      }
    }
  } catch (...) {}
  response.clear(); return false;
}
bool FixtureGraph::restoreControl(const QByteArray& frame, QByteArray& response)
{
  response.clear();
  FixtureRestart::Control control;
  const bool cancel = FixtureRestart::decodeControl(frame, control) &&
      (control.kind == FixtureRestart::ControlKind::UserStop || control.kind == FixtureRestart::ControlKind::NewPlay);
  const auto available = [this, cancel] {
    return cancel ? cleanupSessionReady() : sessionReady(false);
  };
  try {
    if (!session_ || !session_->child || !available()) return false;
    if (cancel) { session_->cancelSequence = control.sequence; session_->cancelRevision = control.revision; }
    if (session_->child->control(restoreToken(HealthyModuleIdentity::EndpointRole::Control), frame, response) &&
        available() && (frame.left(4) != "JRP1" || prepareRestoreAuthority())) return true;
  } catch (...) {}
  response.clear(); return false;
}
bool FixtureGraph::restorePollCancellation(QByteArray& response)
{
  response.clear();
  try {
    if (session_ && session_->child && cleanupSessionReady() && session_->child->pollCancellation(response) &&
        cleanupSessionReady()) return true;
  } catch (...) {}
  response.clear(); return false;
}
bool FixtureGraph::restoreLoad()
{
  return sessionReady(false) && session_ && session_->child && session_->child->load();
}
bool FixtureGraph::restoreSeek()
{
  return sessionReady(false) && session_ && session_->child && captureTargetReady() && session_->child->seek();
}
bool FixtureGraph::recordRestoreFailure(HealthyRestart::FailureCause cause)
{
  if (cause == HealthyRestart::FailureCause::None || !session_ || !session_->gate) return false;
  if (session_->firstFailure == HealthyRestart::FailureCause::None) session_->firstFailure = cause;
  return session_->gate->canceled() || session_->gate->cancel(closeFailedGate, nullptr);
}

bool FixtureGraph::beginRestoreFailure(HealthyRestart::FailureCause cause)
{
  if (cause == HealthyRestart::FailureCause::None || !session_ || !session_->gate || !player_ ||
      access_->phase.load() != AccessPhase::CleanupOnly) return false;
  if (!recordRestoreFailure(cause)) return false;
  const auto claim = std::atomic_load(&access_->intentClaim);
  if (claim != player_->m_healthyStopLease) return false;
  if (!failureCleanupReady()) return false;
  if (!player_->m_nativeRestore.armed) return session_->armInFlight;
  return player_->beginNativeRestoreCleanup(session_->gate, std::nullopt, deadline_, session_->firstFailure);
}

PreparedGraph::FailureProgress FixtureGraph::pollRestoreFailure()
{
  using Progress = PreparedGraph::FailureProgress;
  using Status = PlayerComponent::RestoreCleanupStatus;
  if (!failureCleanupReady() || session_->firstFailure == HealthyRestart::FailureCause::None) return Progress::Unknown;
  if (!player_->m_nativeRestore.armed) return session_->armInFlight ? Progress::Pending : Progress::Unknown;
  if (!player_->m_restoreCleanup.claimed && !beginRestoreFailure(session_->firstFailure)) return Progress::Unknown;
  const auto status = player_->pollNativeRestoreCancellation();
  const auto& state = player_->m_restoreCleanup;
  if (!failureCleanupReady() || state.origin != HealthyRestart::RestoreCleanupState::Origin::InternalFailure ||
      state.firstFailure != session_->firstFailure || !player_->restoreCleanupOwnsCurrent()) return Progress::Unknown;
  return status == Status::Pending ? Progress::Pending :
      status == Status::NoLoad || status == Status::Stopped ? Progress::Qualified : Progress::Unknown;
}

bool FixtureGraph::restoreRevoke()
{
  if (!dispatcher_ || dispatcher_->thread() != QThread::currentThread()) return false;
  bool result = false;
  try { result = session_ && session_->child && session_->child->revoke(); } catch (...) {}
  invalidate();
  return result;
}
}
