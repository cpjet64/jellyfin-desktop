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
  if (!grantOperation_ || settled->operation != grantOperation_ ||
      settled->transferred != settled->bytes.size() ||
      !endpoints_->consumeGrantReadSettlement(grantOperation_)) return false;
  grantOperation_ = 0;
  using Outcome = FixtureRestart::WindowsRestartIo::Settlement::Outcome;
  if (settled->outcome == Outcome::Aborted) {
    grantSettled_ = cancelReceived_;
    return grantSettled_ && (grantStage_ != GrantStage::Eof || validateCanceledGrant());
  }
  if (settled->outcome == Outcome::Uncertain) return false;
  if (settled->outcome == Outcome::Eof) {
    if (grantStage_ != GrantStage::Eof) return false;
    grantSettled_ = true;
    if (cancelReceived_) return validateCanceledGrant();
    QByteArray response;
    if (!grant(grantFrame_, response)) return false;
    const bool accepted = response.left(4) == "JGA2";
    if (!accepted && response.left(4) != "JCN1") return false;
    if (!accepted) { cancelReceived_ = true; cancelTerminal_ = true; }
    if (!queueAck(response, accepted)) return false;
    grantStage_ = GrantStage::Done;
    return true;
  }
  if (settled->outcome != Outcome::Completed || settled->bytes.empty() || grantStage_ == GrantStage::Eof)
    return false;
  grantFrame_.append(reinterpret_cast<const char*>(settled->bytes.data()), qsizetype(settled->bytes.size()));
  if (unsigned(grantFrame_.size()) > grantTarget_) return false;
  if (grantStage_ == GrantStage::Header && grantFrame_.size() == 4) {
    const auto size = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(grantFrame_.constData()));
    if (!size || size > 9212) return false;
    grantTarget_ = 4 + size;
    grantStage_ = GrantStage::Body;
  }
  if (grantStage_ == GrantStage::Body && unsigned(grantFrame_.size()) == grantTarget_) {
    if (cancelReceived_) { grantSettled_ = true; return validateCanceledGrant(); }
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
  if (restorePolling_) return Progress::Pending;
  QScopedValueRollback<bool> guard(restorePolling_, true);
  if (!restoreStarted_) return Progress::Failed;
  if (grantSettled_) return Progress::Ready;
  cancelReceived_ = true;
  if (!pollGrant()) return Progress::Failed;
  return grantSettled_ ? Progress::Ready : Progress::Pending;
}
}
#endif
