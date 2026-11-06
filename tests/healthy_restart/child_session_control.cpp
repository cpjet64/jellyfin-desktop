#include "child_session.h"
#ifdef _WIN32
#include "../fixture_restart/RestartWire.h"
#include <QScopedValueRollback>
#include <QtEndian>

namespace HealthyRetirement {
bool ChildSession::beginControlRead(unsigned size)
{
  controlIo_ = endpoints_->beginTrackedRead(Role::Control, size);
  controlOperation_ = endpoints_->trackedReadOperation(Role::Control);
  return controlOperation_ && (controlIo_ == Io::Pending || controlIo_ == Io::Complete);
}

bool ChildSession::consumeControlBytes(const std::vector<unsigned char>& bytes, bool diagnosticOnly)
{
  const auto malformed = [this] { noteFailure(HealthyRestart::FailureCause::ProtocolViolation); return false; };
  if (bytes.empty()) return malformed();
  controlFrame_.append(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size()));
  if (unsigned(controlFrame_.size()) > controlTarget_) return malformed();
  if (controlStage_ == ControlStage::Header && controlFrame_.size() == 4) {
    if (controlFrame_ != "JRC1" && controlFrame_ != "JRP1") return malformed();
    if (!diagnosticOnly && controlFrame_ == "JRP1" && !grantAck_) return malformed();
    controlTarget_ = controlFrame_ == "JRP1" ? 80 : 24;
    controlStage_ = ControlStage::Body;
  }
  if (controlStage_ != ControlStage::Body || unsigned(controlFrame_.size()) != controlTarget_) return true;
  if (controlFrame_.left(4) == "JRC1") {
    FixtureRestart::Control command;
    if (!FixtureRestart::decodeControl(controlFrame_, command) ||
        (command.kind != FixtureRestart::ControlKind::UserStop && command.kind != FixtureRestart::ControlKind::NewPlay))
      return malformed();
  } else {
    const auto* bytes = reinterpret_cast<const uchar*>(controlFrame_.constData());
    FixtureRestart::RestorePermit expected{qFromBigEndian<quint64>(bytes + 8), qFromBigEndian<quint64>(bytes + 16),
        qFromBigEndian<quint64>(bytes + 24), qFromBigEndian<quint64>(bytes + 32),
        qFromBigEndian<quint64>(bytes + 40), controlFrame_.mid(48, 32)}, parsed;
    if (!FixtureRestart::decodeRestorePermit(controlFrame_, expected, parsed)) return malformed();
  }
  if (diagnosticOnly) return true;
  QByteArray response;
  if (!control(controlFrame_, response)) return false;
  if (controlFrame_.left(4) == "JRC1") cancelReceived_ = true;
  if (!response.isEmpty() && cancelReceived_) cancelTerminal_ = true;
  if (!response.isEmpty() && !queueAck(response, false)) return false;
  controlStage_ = ControlStage::Header;
  controlTarget_ = 4;
  controlFrame_.clear();
  return true;
}

bool ChildSession::pollControl()
{
  if (!ackFrame_.isEmpty()) return true;
  auto settlement = endpoints_->trackedReadSettlement(Role::Control);
  if (!settlement) {
    if (controlIo_ == Io::Pending || controlIo_ == Io::Unconfirmed) controlIo_ = endpoints_->poll(Role::Control);
    settlement = endpoints_->trackedReadSettlement(Role::Control);
    if (!settlement) return controlIo_ == Io::Pending;
  }
  using Outcome = FixtureRestart::WindowsRestartIo::Settlement::Outcome;
  if (!controlOperation_ || settlement->operation != controlOperation_ ||
      settlement->outcome != Outcome::Completed || settlement->transferred != settlement->bytes.size()) {
    noteFailure(settlement->outcome == Outcome::Uncertain ? HealthyRestart::FailureCause::OwnershipLost :
                HealthyRestart::FailureCause::EndpointLost);
    return false;
  }
  const auto bytes = settlement->bytes;
  if (!endpoints_->consumeTrackedReadSettlement(Role::Control, controlOperation_)) return false;
  controlOperation_ = 0;
  if (!consumeControlBytes(bytes, false)) return false;
  if (failed_) return false;
  return beginControlRead(controlTarget_ - unsigned(controlFrame_.size()));
}

ChildSession::Progress ChildSession::settleControlRead()
{
  if (!failed_) return Progress::Failed;
  if (controlSettlementFailed_) return Progress::Failed;
  if (controlSettled_) return Progress::Ready;
  if (endpoints_->containmentUncertain(Role::Control)) return Progress::Failed;
  if (!controlOperation_) { controlSettled_ = true; return Progress::Ready; }
  if (failurePolling_ || restorePolling_) return Progress::Pending;
  QScopedValueRollback<bool> guard(restorePolling_, true);
  auto settlement = endpoints_->retainedSettlement(Role::Control);
  if (!settlement) {
    const auto result = controlContainCancelStarted_ ? endpoints_->poll(Role::Control) : endpoints_->cancel(Role::Control);
    controlContainCancelStarted_ = true;
    settlement = endpoints_->retainedSettlement(Role::Control);
    if (!settlement) return result == Io::Pending || result == Io::Unconfirmed ? Progress::Pending : Progress::Failed;
  }
  using Outcome = FixtureRestart::WindowsRestartIo::Settlement::Outcome;
  if (!settlement->read || settlement->operation != controlOperation_ ||
      settlement->transferred != settlement->bytes.size() || settlement->outcome == Outcome::Uncertain ||
      ((settlement->outcome == Outcome::Aborted || settlement->outcome == Outcome::Eof) && settlement->transferred))
    return Progress::Failed;
  const auto bytes = settlement->bytes;
  const auto outcome = settlement->outcome;
  if (!endpoints_->consumeRetainedSettlement(Role::Control, controlOperation_)) return Progress::Failed;
  controlOperation_ = 0;
  controlSettled_ = true;
  if (outcome == Outcome::Completed && !consumeControlBytes(bytes, true)) {
    noteFailure(HealthyRestart::FailureCause::ProtocolViolation);
    controlSettlementFailed_ = true; return Progress::Failed;
  }
  if (outcome == Outcome::Eof && !controlFrame_.isEmpty())
    noteFailure(HealthyRestart::FailureCause::ProtocolViolation);
  return Progress::Ready;
}
}
#endif
