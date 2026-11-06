#include "child_session.h"
#ifdef _WIN32
#include "module_channel.h"
#include "module_gate.h"
#include "../fixture_restart/RestartBroker.h"
#include <QtEndian>
#include <QScopedValueRollback>

namespace HealthyRetirement {
bool ChildSession::pollGrant()
{
  const auto malformed = [this] { noteFailure(HealthyRestart::FailureCause::ProtocolViolation); return false; };
  const auto validate = [this, &malformed] {
    if (validateCanceledGrant()) return true;
    return valid() ? malformed() : false;
  };
  if (grantSettled_) return true;
  auto settled = endpoints_->grantReadSettlement();
  if (!settled) {
    if (cancelReceived_ && !grantCancelStarted_) {
      grantCancelStarted_ = true;
      grantIo_ = endpoints_->cancel(Role::Grant);
    } else if (grantIo_ == Io::Pending || grantIo_ == Io::Unconfirmed) {
      grantIo_ = endpoints_->poll(Role::Grant);
    }
    settled = endpoints_->grantReadSettlement();
    if (!settled) return grantIo_ == Io::Pending || grantIo_ == Io::Unconfirmed;
  }
  using Outcome = FixtureRestart::WindowsRestartIo::Settlement::Outcome;
  if (!grantOperation_ || settled->operation != grantOperation_ || settled->outcome == Outcome::Uncertain ||
      settled->transferred != settled->bytes.size() ||
      !endpoints_->consumeGrantReadSettlement(grantOperation_)) return false;
  grantOperation_ = 0;
  if (settled->outcome == Outcome::Aborted) {
    grantSettled_ = cancelReceived_;
    if (!grantSettled_) return false;
    return grantStage_ != GrantStage::Eof || validate();
  }
  if (settled->outcome == Outcome::Eof) {
    if (grantStage_ != GrantStage::Eof) return malformed();
    grantSettled_ = true;
    if (!validate()) return false;
    if (cancelReceived_) return true;
    if (failed_ || !valid()) return false;
    QByteArray response;
    if (!grant(grantFrame_, response)) return false;
    const bool accepted = response.left(4) == "JGA2";
    if (!accepted && response.left(4) != "JCN1") return false;
    if (!accepted) { cancelReceived_ = true; cancelTerminal_ = true; }
    if (!queueAck(response, accepted)) return false;
    grantStage_ = GrantStage::Done;
    return true;
  }
  if (settled->outcome != Outcome::Completed || settled->bytes.empty()) return false;
  if (grantStage_ == GrantStage::Eof) return malformed();
  grantFrame_.append(reinterpret_cast<const char*>(settled->bytes.data()), qsizetype(settled->bytes.size()));
  if (unsigned(grantFrame_.size()) > grantTarget_) return malformed();
  if (grantStage_ == GrantStage::Header && grantFrame_.size() == 4) {
    const auto size = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(grantFrame_.constData()));
    if (!size || size > 9212) return malformed();
    grantTarget_ = 4 + size;
    grantStage_ = GrantStage::Body;
  }
  if (grantStage_ == GrantStage::Body && unsigned(grantFrame_.size()) == grantTarget_) {
    if (cancelReceived_) { grantSettled_ = true; return validate(); }
    grantStage_ = GrantStage::Eof;
  }
  if (cancelReceived_) { grantSettled_ = true; return true; }
  return beginGrantRead(grantStage_ == GrantStage::Eof ? 1 : grantTarget_ - unsigned(grantFrame_.size()));
}

bool ChildSession::beginGrantRead(unsigned size)
{
  grantIo_ = endpoints_->beginTrackedGrantRead(size);
  grantOperation_ = endpoints_->grantReadOperation();
  return grantOperation_ && (grantIo_ == Io::Pending || grantIo_ == Io::Complete || grantIo_ == Io::Eof);
}

bool ChildSession::validateCanceledGrant() const
{
#ifdef HEALTHY_CHILD_SESSION_TESTING
  if (testing_) return test_.validateCanceledGrant && test_.validateCanceledGrant(grantFrame_);
#endif
#ifndef HEALTHY_CHILD_SESSION_HEADLESS
  const auto capability = endpoints_->capability();
  HealthyModuleIdentity::LaunchBinding binding;
  if (!capability || !capability->launchBinding(generation_, binding) ||
      binding.role != HealthyModuleIdentity::LaunchRole::Restore) return false;
  const QByteArray digest(reinterpret_cast<const char*>(binding.expectedGrantDigest.data()),
                          int(binding.expectedGrantDigest.size()));
  FixtureRestart::Grant ignored;
  return FixtureRestart::decodeCommittedGrant(grantFrame_, binding.run, digest, ignored);
#else
  return false;
#endif
}

ChildSession::Progress ChildSession::settleGrantRead()
{
  if (restorePolling_ || failurePolling_) return Progress::Pending;
  QScopedValueRollback<bool> guard(restorePolling_, true);
  if (!restoreStarted_) return Progress::Failed;
  if (grantSettlementFailed_) return Progress::Failed;
  if (grantSettled_) return Progress::Ready;
  if (!grantOperation_) { grantSettled_ = true; return Progress::Ready; }
  cancelReceived_ = true;
  if (!pollGrant()) { grantSettlementFailed_ = true; return Progress::Failed; }
  return grantSettled_ ? Progress::Ready : Progress::Pending;
}
}
#endif
