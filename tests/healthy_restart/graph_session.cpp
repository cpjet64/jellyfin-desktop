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
    session_->gate = std::make_shared<FixtureRestart::ChildGate>(grantChannel, normalDeadline_);
    session_->adapter = std::make_unique<FixtureRestartAdapter>(*player_, *session_->gate, freshId(), freshId(),
                                                                launchId, digest, clock_);
    session_->child = std::make_unique<FixtureRestart::RestartChildPlayer>(*session_->adapter, *session_->gate,
        grantChannel, endpoints_->token(EndpointRole::Control), launch.run, launchId, digest, path, clock_);
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
                               frame, clock_(), eof, response) && sessionReady(false) &&
        installRetirementClaim() && sessionReady(false)) return true;
  } catch (...) {}
  response.clear(); invalidate(); return false;
}
bool FixtureGraph::restoreControl(const QByteArray& frame, QByteArray& response)
{
  response.clear();
  if (!sessionReady(false) || !session_ || !session_->child) return false;
  try {
    if (session_->child->control(endpoints_->token(HealthyModuleIdentity::EndpointRole::Control), frame, response) &&
        sessionReady(false)) return true;
  } catch (...) {}
  response.clear(); invalidate(); return false;
}
bool FixtureGraph::restoreLoad()
{
  // Held until B cancellation has an observed, nonblocking cleanup path.
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
