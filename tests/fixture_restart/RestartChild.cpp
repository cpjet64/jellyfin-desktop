#include "RestartChild.h"
#include <QtEndian>
#include <utility>

namespace FixtureRestart {
namespace {
bool closedGate(void*) { return true; }
bool sameIdentity(const RestartChild::ArmIdentity& a, const RestartChild::ArmIdentity& b)
{
  return a.host == b.host && a.core == b.core && a.intentRevision == b.intentRevision && a.attempt == b.attempt;
}
bool validIdentity(const RestartChild::ArmIdentity& identity)
{
  return identity.host && identity.core && identity.intentRevision && identity.attempt;
}
}
RestartChild::RestartChild(ChildGate& gate, const void* grantChannel, const void* controlChannel,
                           Grant binding, Actions actions)
  : gate_(gate), grantChannel_(grantChannel), controlChannel_(controlChannel),
    binding_(std::move(binding)), actions_(std::move(actions)), currentRevision_(binding_.revision)
{
}

RestartChild::RestartChild(ChildGate& gate, const void* grantChannel, const void* controlChannel,
                           Grant binding, quint64 launch, QByteArray expectedDigest, Actions actions)
  : gate_(gate), grantChannel_(grantChannel), controlChannel_(controlChannel),
    binding_(std::move(binding)), launch_(launch), expectedDigest_(std::move(expectedDigest)),
    actions_(std::move(actions)), currentRevision_(binding_.revision)
{
}

RestartChild::RestartChild(ChildGate& gate, const void* grantChannel, const void* controlChannel,
                           quint64 expectedRun, quint64 launch, QByteArray expectedDigest, Actions actions)
  : gate_(gate), grantChannel_(grantChannel), controlChannel_(controlChannel),
    launch_(launch), expectedDigest_(std::move(expectedDigest)), actions_(std::move(actions))
{
  binding_.run = expectedRun;
}

bool RestartChild::grant(const void* channel, const QByteArray& frame, qint64 now, bool eof)
{
  Q_UNUSED(channel); Q_UNUSED(frame); Q_UNUSED(now); Q_UNUSED(eof);
  return false;
}

bool RestartChild::grant(const void* channel, const QByteArray& frame, qint64 now, bool eof,
                         QByteArray& response)
{
  if (!grantChannel_ || channel != grantChannel_) return false;
  if (currentRevision_ != binding_.revision) return false;
  Grant decoded;
  if (!decodeCommittedGrant(frame, binding_.run, expectedDigest_, decoded)) { protocolFailed_ = true; revoke(); return false; }
  if (!binding_.grant) { binding_ = decoded; currentRevision_ = decoded.revision; }
  if (granted_ || !actions_.armActual || !launch_ || expectedDigest_.size() != 32 ||
      frame != encodeGrant(binding_) ||
      !gate_.accept(channel, frame, binding_.run, expectedDigest_, now, eof)) {
    protocolFailed_ = true; revoke(); return false;
  }
  armPhase_ = ArmPhase::InFlight;
#ifdef HEALTHY_CHILD_RESTART_TESTING
  if (actions_.beforeArm) actions_.beforeArm();
#endif
  if (!gate_.allowed(binding_.revision)) {
    armPhase_ = ArmPhase::NotArmed;
    if (cancelPhase_ == CancelPhase::AwaitArm) return pollCancellation(response);
    return false;
  }
  const auto armed = actions_.armActual();
  armPhase_ = armed.status == ArmResult::Status::Armed ? ArmPhase::Armed :
      armed.status == ArmResult::Status::NotArmed ? ArmPhase::NotArmed : ArmPhase::Unknown;
  if (armPhase_ != ArmPhase::Armed || !validIdentity(armed.identity)) {
    if (armPhase_ == ArmPhase::Armed) armPhase_ = ArmPhase::Unknown;
    if (armPhase_ == ArmPhase::NotArmed && cancelPhase_ == CancelPhase::AwaitArm)
      return pollCancellation(response);
    return false;
  }
  identity_ = armed.identity;
  granted_ = true;
  const auto ack = encodeGrantAccepted({binding_.run, launch_, binding_.grant,
                                        binding_.revision, expectedDigest_, identity_.host,
                                        identity_.core, identity_.intentRevision, identity_.attempt});
  if (ack.isEmpty()) return false;
  response = ack;
  return true;
}

bool RestartChild::control(const void* channel, const QByteArray& request, QByteArray& response)
{
  if (!controlChannel_ || channel != controlChannel_) return false;
  if (request.size() == 80 && request.left(4) == "JRP1") {
    const auto sequence = qFromBigEndian<quint64>(reinterpret_cast<const uchar*>(request.constData() + 40));
    RestorePermit permit, expected{binding_.run, launch_, binding_.grant, binding_.revision,
                                   sequence, expectedDigest_};
    if (!granted_ || sequence != 1 || sequence <= lastSequence_ || !actions_.now ||
        !decodeRestorePermit(request, expected, permit) ||
        !gate_.installPermit(request, expected, actions_.now())) { revoke(); return false; }
    lastSequence_ = sequence;
    return true;
  }
  Control control;
  if (!decodeControl(request, control) ||
      (control.kind != ControlKind::UserStop && control.kind != ControlKind::NewPlay) ||
      control.sequence <= lastSequence_ || control.revision <= currentRevision_) {
    revoke(); return false;
  }
  lastSequence_ = control.sequence;
  currentRevision_ = control.revision;
  if (!beginCancellation(control)) return false;
  return pollCancellation(response);
}

bool RestartChild::beginCancellation(const Control& control)
{
  if (cancelPhase_ != CancelPhase::None || gate_.canceled() || !gate_.cancel(closedGate, nullptr)) return false;
  cancelSequence_ = control.sequence;
  cancelRevision_ = control.revision;
  cancelPhase_ = CancelPhase::AwaitArm;
  return true;
}

bool RestartChild::startCleanup()
{
  if (!actions_.beginCleanup || !actions_.pollCleanup) {
    cleanupStartFailed_ = true; cancelPhase_ = CancelPhase::Cleanup; return true;
  }
  cancelPhase_ = CancelPhase::Starting;
  if (!actions_.beginCleanup(cancelSequence_, cancelRevision_)) {
    cleanupStartFailed_ = true;
  }
  cancelPhase_ = CancelPhase::Cleanup;
  return true;
}

bool RestartChild::pollCancellation(QByteArray& response)
{
  if (cancelPhase_ == CancelPhase::None || cancelPhase_ == CancelPhase::Done || cancelPhase_ == CancelPhase::Failed)
    return false;
  if (cancelPhase_ == CancelPhase::Starting || cancelPhase_ == CancelPhase::Polling) return true;
  if (cancelPhase_ == CancelPhase::AwaitArm) {
    if (armPhase_ == ArmPhase::InFlight) return true;
    if (protocolFailed_ || armPhase_ == ArmPhase::Unknown) { cancelPhase_ = CancelPhase::Failed; return false; }
    if (armPhase_ == ArmPhase::Unstarted || armPhase_ == ArmPhase::NotArmed) {
      const auto frame = encodeCancelNoArm({binding_.run, launch_, cancelRevision_, cancelSequence_, expectedDigest_});
      if (frame.isEmpty()) { cancelPhase_ = CancelPhase::Failed; return false; }
      cancelPhase_ = CancelPhase::Done;
      response = frame;
      return true;
    }
    if (!startCleanup()) return false;
  }
  cancelPhase_ = CancelPhase::Polling;
  const auto result = cleanupStartFailed_ ? CleanupResult{CleanupResult::Status::Failed, identity_,
      CancelFailure::CleanupUnconfirmed} : actions_.pollCleanup();
  cancelPhase_ = CancelPhase::Cleanup;
  if (result.status == CleanupResult::Status::Pending) return true;
  if (result.status != CleanupResult::Status::Failed && !sameIdentity(result.identity, identity_)) {
    cancelPhase_ = CancelPhase::Failed; return false;
  }
  const auto outcome = result.status == CleanupResult::Status::NoLoad ? CancelOutcome::NoLoad :
      result.status == CleanupResult::Status::Stopped ? CancelOutcome::Stopped : CancelOutcome::Failed;
  const auto frame = encodeCancelResult({binding_.run, launch_, binding_.grant, binding_.revision, cancelSequence_,
      identity_.host, identity_.core, identity_.intentRevision, identity_.attempt, outcome, result.reason});
  if (frame.isEmpty()) { cancelPhase_ = CancelPhase::Failed; return false; }
  cancelPhase_ = CancelPhase::Done;
  response = frame;
  return true;
}

bool RestartChild::allowed() const
{
  return granted_ && actions_.now && gate_.allowsRestore(binding_.run, launch_, binding_.grant,
      binding_.revision, expectedDigest_, actions_.now());
}

bool RestartChild::revoke()
{
  if (gate_.canceled()) return false;
  return gate_.cancel(closedGate, nullptr);
}

bool RestartChild::load() { return allowed() && actions_.load && actions_.load() && allowed(); }
bool RestartChild::seek() { return allowed() && actions_.seek && actions_.seek() && allowed(); }
}
