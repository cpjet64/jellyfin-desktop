#include "graph_session.h"
#include "fixture_restart/FixtureRestartAdapter.h"
#include "fixture_restart/RestartChildPlayer.h"
#include "fixture_restart/RestartSourcePlayer.h"
#include "module_endpoints.h"
#include "player/PlayerComponent.h"
#include <QRandomGenerator>
#include <QThread>

namespace HealthyRetirement {
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
}
FixtureGraph::Session::Session() = default;
FixtureGraph::Session::~Session() = default;

bool FixtureGraph::sessionReady(bool source) const
{
  const auto containment = containment_.lock();
  if (!dispatcher_ || dispatcher_->thread() != QThread::currentThread() || !containment ||
      !containment->intact() || !player_ || !module_ || !endpoints_ || retirementRequested_) return false;
  HealthyModuleIdentity::LaunchBinding binding;
  try {
    const auto now = clock_();
    if (now < lastSessionNow_ || now >= normalDeadline_) return false;
    lastSessionNow_ = now;
    return module_->stillValid(slot_->generation()) &&
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
      !module_ || !endpoints_ || retirementRequested_) return false;
  try {
    HealthyModuleIdentity::LaunchBinding binding;
    const auto now = clock_();
    if (now < lastSessionNow_ || now >= deadline_ || !containment->intact() || !module_->stillValid(slot_->generation()) ||
        !module_->launchBinding(slot_->generation(), binding) ||
        binding.role != HealthyModuleIdentity::LaunchRole::Restore ||
        !endpoints_->token(HealthyModuleIdentity::EndpointRole::Control) ||
        !endpoints_->token(HealthyModuleIdentity::EndpointRole::Ack)) return false;
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

bool FixtureGraph::restoreInitialize(const QString& path)
{
  using namespace HealthyModuleIdentity;
  if (!sessionReady(false) || session_) return false;
  const auto grantChannel = endpoints_->token(EndpointRole::Grant);
  if (!grantChannel) return false;
  try {
    LaunchBinding launch;
    std::uint64_t launchId = 0;
    if (!module_->launchBinding(slot_->generation(), launch) ||
        !module_->launchId(slot_->generation(), launchId)) { invalidate(); return false; }
    const QByteArray digest(reinterpret_cast<const char*>(launch.expectedGrantDigest.data()),
                            int(launch.expectedGrantDigest.size()));
    session_ = std::make_unique<Session>();
    session_->run = launch.run; session_->launch = launchId; session_->digest = digest;
    session_->gate = std::make_shared<FixtureRestart::ChildGate>(grantChannel, normalDeadline_);
    session_->adapter = std::make_unique<FixtureRestartAdapter>(*player_, *session_->gate, freshId(), freshId(),
                                                                launchId, digest, clock_);
    if (!session_->adapter->bindCleanupGate(session_->gate)) { invalidate(); return false; }
    FixtureRestart::RestartChild::Actions actions;
    actions.now = clock_;
    actions.armActual = [this] {
      using Result = FixtureRestart::RestartChild::ArmResult;
      const auto* grant = session_->gate->acceptedGrant();
      if (!grant || !sessionReady(false))
        return session_->gate->canceled() ? session_->adapter->armActual() : Result{};
      const auto binding = *grant;
      const auto result = session_->adapter->armActual();
      if (result.status != Result::Status::Armed) return result;
      session_->binding = binding;
      session_->host = result.identity.host; session_->core = result.identity.core;
      session_->intentRevision = result.identity.intentRevision; session_->attempt = result.identity.attempt;
      const bool claimInstalled = installRetirementClaim();
      if ((!claimInstalled || session_->gate->canceled()) && player_->m_healthyStopLease)
        player_->m_healthyStopLease->revoke();
      if (!claimInstalled) return Result{}; // Actual identity remains in Session for containment.
      return result;
    };
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
    actions.load = [this, path, launchId, digest] {
      const auto& binding = session_->binding;
      if (!player_->m_restoreGate && !player_->healthyBindRestoreGate(session_->gate, binding.run, launchId,
          binding.grant, binding.revision, digest, clock_)) return false;
      return sessionReady(false) && session_->adapter->load(path) && sessionReady(false);
    };
    actions.seek = [] { return false; }; // Held for renderer-issued target-arm capability.
    session_->child = std::make_unique<FixtureRestart::RestartChildPlayer>(*session_->gate, grantChannel,
        endpoints_->token(EndpointRole::Control), launch.run, launchId, digest, std::move(actions));
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
    if (session_->child->grant(endpoints_->token(HealthyModuleIdentity::EndpointRole::Grant),
                               frame, clock_(), eof, response)) {
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
      if ((armed || unarmed) && now >= lastSessionNow_ && now < deadline_ && module_ &&
          module_->stillValid(slot_->generation()) && endpoints_ &&
          endpoints_->token(HealthyModuleIdentity::EndpointRole::Grant) &&
          endpoints_->token(HealthyModuleIdentity::EndpointRole::Ack)) {
        lastSessionNow_ = now;
        return true;
      }
    }
  } catch (...) {}
  response.clear(); invalidate(); return false;
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
    if (session_->child->control(endpoints_->token(HealthyModuleIdentity::EndpointRole::Control), frame, response) &&
        available()) return true;
  } catch (...) {}
  response.clear(); invalidate(); return false;
}
bool FixtureGraph::restorePollCancellation(QByteArray& response)
{
  response.clear();
  try {
    if (session_ && session_->child && cleanupSessionReady() && session_->child->pollCancellation(response) &&
        cleanupSessionReady()) return true;
  } catch (...) {}
  response.clear(); invalidate(); return false;
}
bool FixtureGraph::restoreLoad()
{
  // Held until failure-origin cleanup is retained and pumped by ChildSession.
  return false;
}
bool FixtureGraph::restoreSeek()
{
  return false;
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
