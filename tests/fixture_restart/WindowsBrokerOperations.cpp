#include "WindowsBrokerOperations.h"
#ifdef _WIN32
#include "../healthy_restart/module_gate.h"
#include <QCryptographicHash>
#include <QScopedValueRollback>
#include <limits>
#include <algorithm>
#include <utility>
#include <array>

namespace FixtureRestart {
WindowsBrokerOperations::WindowsBrokerOperations(WindowsRestartProcess& a, WindowsRestartProcess& b,
    WindowsEndpoint evidence, WindowsEndpoint aControl, WindowsEndpoint aAck, WindowsEndpoint bGrant,
    WindowsEndpoint bControl, WindowsEndpoint bAck, std::wstring executable, std::wstring arguments, quint64 revision,
    decltype(&PeekNamedPipe) peek)
  : a_(&a), b_(&b), evidence_(evidence), aControl_(aControl), aAck_(aAck), bGrant_(bGrant),
    bControl_(bControl), bAck_(bAck), executable_(std::move(executable)), arguments_(std::move(arguments)), revision_(revision), peek_(peek)
{
  const std::array<WindowsEndpoint, 6> endpoints{evidence_, aControl_, aAck_, bGrant_, bControl_, bAck_};
  valid_ = a_ != b_ && revision_ && !executable_.empty();
  for (size_t i = 0; valid_ && i < endpoints.size(); ++i) {
    const auto endpoint = endpoints[i];
    auto* process = i < 3 ? a_ : b_;
    valid_ = endpoint.channel && endpoint.io && process->ownsEndpoint(endpoint.channel, endpoint.io);
    for (size_t j = 0; valid_ && j < i; ++j)
      valid_ = endpoint.channel != endpoints[j].channel && endpoint.io != endpoints[j].io;
  }
}

qint64 WindowsBrokerOperations::nowMs() const { return qint64(GetTickCount64()); }

bool WindowsBrokerOperations::ownsAChannel(const void* channel) const
{
  return valid_ && a_ && evidence_.channel && channel == evidence_.channel && a_->launchEstablished() &&
      a_->ownsEndpoint(evidence_.channel, evidence_.io) && a_->pollRunning() == ProcessProgress::Complete;
}

bool WindowsBrokerOperations::validateEvidence(const Evidence& evidence) const
{
  return valid_ && validateFixtureEvidence(evidence);
}

Operations::Progress WindowsBrokerOperations::write(WindowsEndpoint endpoint, const QByteArray& bytes,
                                                    int& stage, qint64 deadline)
{
  if (!valid_ || !endpoint.channel || !endpoint.io || endpoint.channel->parentHandle() == INVALID_HANDLE_VALUE || bytes.isEmpty()) return Progress::Failed;
  if (stage == 2) return Progress::Complete;
  if (stage < 0) return Progress::Failed;
  const auto result = stage == 0 ? endpoint.io->begin(endpoint.channel->parentHandle(), false,
      bytes.constData(), DWORD(bytes.size()), ULONGLONG(deadline)) : endpoint.io->poll();
  if (result == IoProgress::Complete) { stage = 2; return Progress::Complete; }
  if (result == IoProgress::Failed || result == IoProgress::Eof) { stage = -1; return Progress::Failed; }
  stage = 1;
  return Progress::Pending;
}

Operations::Progress WindowsBrokerOperations::exchange(Exchange& state, WindowsEndpoint writer,
    WindowsEndpoint reader, const Control& control, qint64 deadline)
{
  if (state.request.isEmpty()) state.request = encodeControl(control);
  if (state.request.isEmpty()) return Progress::Failed;
  if (state.stage == 4) return Progress::Complete;
  if (state.stage < 2) {
    const auto result = write(writer, state.request, state.stage, deadline);
    if (result != Progress::Complete) return result;
  }
  if (!reader.channel || !reader.io || reader.channel->parentHandle() == INVALID_HANDLE_VALUE) return Progress::Failed;
  const auto result = state.stage == 2 ? reader.io->begin(reader.channel->parentHandle(), true, nullptr, 24,
      ULONGLONG(deadline)) : reader.io->poll();
  if (result == IoProgress::Pending || result == IoProgress::Unconfirmed) { state.stage = 3; return Progress::Pending; }
  if (result != IoProgress::Complete) { state.stage = -1; return Progress::Failed; }
  const auto& bytes = reader.io->bytes();
  Control ack;
  if (!decodeControl(QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size())), ack) ||
      ack.kind != ControlKind::Ack || ack.sequence != control.sequence || ack.revision != control.revision) {
    state.stage = -1; return Progress::Failed;
  }
  state.stage = 4;
  return Progress::Complete;
}

Operations::Progress WindowsBrokerOperations::internalReplacementStop(qint64 deadline)
{
  return exchange(aStop_, aControl_, aAck_, {ControlKind::InternalStop, 1, revision_}, deadline);
}

Operations::Progress WindowsBrokerOperations::read(WindowsEndpoint endpoint, DWORD size, int& stage,
                                                   qint64 deadline, bool expectEof)
{
  if (!valid_ || !endpoint.channel || !endpoint.io || stage < 0 || deadline < 0) return Progress::Failed;
  if (stage == 2) return Progress::Complete;
  const auto result = stage == 0 ? endpoint.io->begin(endpoint.channel->parentHandle(), true, nullptr, size,
      ULONGLONG(deadline)) : endpoint.io->poll();
  if (result == IoProgress::Pending || result == IoProgress::Unconfirmed) { stage = 1; return Progress::Pending; }
  if ((expectEof && result == IoProgress::Eof) || (!expectEof && result == IoProgress::Complete)) {
    stage = 2; return Progress::Complete;
  }
  stage = -1;
  return Progress::Failed;
}

Operations::Progress WindowsBrokerOperations::waitAClosed(qint64 deadline)
{
  if (!healthyConfigured_ || aStop_.stage != 4 || !a_->ownsEndpoint(aAck_.channel, aAck_.io)) return Progress::Failed;
  if (aClosed_) return Progress::Complete;
  const auto status = read(aAck_, 64, aClosedStage_, deadline, false);
  if (status != Progress::Complete) return status;
  const auto& data = aAck_.io->bytes();
  Closed closed;
  if (!decodeClosed(QByteArray(reinterpret_cast<const char*>(data.data()), qsizetype(data.size())), expectedClosed_, closed))
    return Progress::Failed;
  aClosed_ = true;
  return Progress::Complete;
}

Operations::Progress WindowsBrokerOperations::waitAAndChildren(qint64 deadline, bool& forced)
{
  forced = false;
  if (!valid_ || !a_ || !aClosed_ || !a_->ownsEndpoint(aAck_.channel, aAck_.io)) return Progress::Failed;
  if (!aProcessSettled_) {
    const auto result = a_->pollHealthy(ULONGLONG(deadline));
    if (result == ProcessProgress::Pending || result == ProcessProgress::Unconfirmed) return Progress::Pending;
    if (result != ProcessProgress::Complete) return Progress::Failed;
    aProcessSettled_ = true;
  }
  const auto eof = read(aAck_, 1, aEofStage_, deadline, true);
  if (eof == Progress::Complete) aSettled_ = true;
  return eof;
}

Operations::Progress WindowsBrokerOperations::launchB(qint64 deadline)
{
  Q_UNUSED(deadline);
  return Progress::Failed;
}

bool WindowsBrokerOperations::configureHealthyTransfer(const Closed& expected, quint64 bLaunch,
    WindowsRestartChannel* identity, std::shared_ptr<HealthyModuleIdentity::StageLease>& lease,
    std::function<bool(const Grant&)> currentIntent)
{
  if (!valid_ || healthyConfigured_ || bLaunchStarted_ || !b_ || !identity || !b_->ownsChannel(identity) ||
      identity == bGrant_.channel || identity == bControl_.channel || identity == bAck_.channel ||
      identity == evidence_.channel || identity == aControl_.channel || identity == aAck_.channel ||
      !lease || !currentIntent || !bLaunch || bLaunch == expected.launch || encodeClosed(expected).isEmpty() ||
      expected.stopSequence != 1 || expected.revision != revision_) return false;
  expectedClosed_ = expected;
  bLaunchId_ = bLaunch;
  bIdentity_ = identity;
  bLease_ = std::move(lease);
  currentIntent_ = std::move(currentIntent);
  healthyConfigured_ = true;
  return true;
}

bool WindowsBrokerOperations::eligibleForB(const Grant& grant) const
{
  if (!healthyConfigured_ || !currentIntent_) return false;
  try { return currentIntent_(grant); } catch (...) { return false; }
}

Operations::Progress WindowsBrokerOperations::launchB(const Grant& grant, const QByteArray& digest, qint64 deadline)
{
  const QByteArray frame = encodeGrant(grant);
  if (!valid_ || !healthyConfigured_ || !aSettled_ || !b_ || grant.run != expectedClosed_.run ||
      grant.revision != expectedClosed_.revision || frame.isEmpty() || digest.size() != 32 ||
      QCryptographicHash::hash(frame, QCryptographicHash::Sha256) != digest) return Progress::Failed;
  if (!bIdentityBound_) {
    HealthyModuleIdentity::LaunchBinding binding;
    binding.role = HealthyModuleIdentity::LaunchRole::Restore;
    binding.run = grant.run;
    std::copy(digest.cbegin(), digest.cend(), binding.expectedGrantDigest.begin());
    if (!b_->bindHealthyModuleLease(bLease_, bIdentity_, bLaunchId_, binding)) return Progress::Failed;
    expectedAcceptance_ = {grant.run, bLaunchId_, grant.grant, grant.revision, digest};
    bIdentityBound_ = true;
  } else if (expectedAcceptance_.run != grant.run || expectedAcceptance_.launch != bLaunchId_ ||
             expectedAcceptance_.grant != grant.grant || expectedAcceptance_.revision != grant.revision ||
             expectedAcceptance_.digest != digest) return Progress::Failed;
  if (!valid_ || !aSettled_ || !b_) return Progress::Failed;
  if (!bProcessLaunched_) {
    const bool started = bLaunchStarted_;
    bLaunchStarted_ = true;
    const auto result = started ? b_->pollLaunch() : b_->launch(executable_, arguments_, ULONGLONG(deadline));
    if (result == ProcessProgress::Pending || result == ProcessProgress::Unconfirmed) return Progress::Pending;
    if (result != ProcessProgress::Complete) return Progress::Failed;
    bProcessLaunched_ = true;
  }
  const auto identityStatus = b_->sendHealthyModuleFrame(ULONGLONG(deadline));
  bIdentityFramePending_ = identityStatus == ProcessProgress::Pending || identityStatus == ProcessProgress::Unconfirmed;
  if (identityStatus == ProcessProgress::Complete) return Progress::Complete;
  return identityStatus == ProcessProgress::Pending || identityStatus == ProcessProgress::Unconfirmed ? Progress::Pending : Progress::Failed;
}

Operations::Progress WindowsBrokerOperations::transferToB(const QByteArray& frame, qint64 deadline)
{
  if (!valid_ || !aSettled_ || !bIdentityBound_ || !b_ || !b_->launchEstablished() || frame.size() > 9216 ||
      QCryptographicHash::hash(frame, QCryptographicHash::Sha256) != expectedAcceptance_.digest) return Progress::Failed;
  const auto result = write(bGrant_, frame, grantStage_, deadline);
  if (result != Progress::Complete) return result;
  return b_->closeParentChannel(bGrant_.channel) ? Progress::Complete : Progress::Failed;
}

Operations::Progress WindowsBrokerOperations::waitBGrantAccepted(qint64 deadline)
{
  if (!bIdentityBound_ || !b_ || !b_->ownsEndpoint(bAck_.channel, bAck_.io)) return Progress::Failed;
  if (bAccepted_) return Progress::Complete;
  const auto status = trackedBRead(nextBReadSize(), deadline);
  if (status == IoProgress::Pending || status == IoProgress::Unconfirmed) return Progress::Pending;
  if (status != IoProgress::Complete) return Progress::Failed;
  return bAccepted_ ? Progress::Complete : Progress::Pending;
}

bool WindowsBrokerOperations::parseBBytes()
{
  if (bReadBytes_.size() < 4) return true;
  const QByteArray magic = bReadBytes_.left(4);
  const int size = magic == "JGA2" ? 104 : magic == "JRC1" ? 24 : magic == "JCN1" ? 72 : magic == "JCS1" ? 80 : 0;
  if (!size || bReadBytes_.size() > size) { bProtocolFailed_ = true; return false; }
  if (bReadBytes_.size() < size) return true;
  bool valid = false;
  if (magic == "JGA2" && !bAccepted_ && !cancelTerminal_) {
    GrantAccepted accepted;
    valid = decodeGrantAccepted(bReadBytes_, expectedAcceptance_, accepted);
    if (valid) { expectedAcceptance_ = accepted; bAccepted_ = true; }
  } else if (cancelStarted_ && magic == "JRC1" && !cancelAck_) {
    Control ack;
    valid = decodeControl(bReadBytes_, ack) && ack.kind == ControlKind::Ack && ack.sequence == 2 && ack.revision == revision_ + 1;
    if (valid) cancelAck_ = true;
  } else if (cancelStarted_ && cancelAck_ && !cancelTerminal_ && magic == "JCN1" && !bAccepted_) {
    valid = decodeCancelNoArm(bReadBytes_, {expectedAcceptance_.run, bLaunchId_, revision_ + 1, 2, expectedAcceptance_.digest});
    if (valid) cancelTerminal_ = true;
  } else if (cancelStarted_ && cancelAck_ && !cancelTerminal_ && magic == "JCS1" && bAccepted_) {
    CancelResult result;
    const CancelResult expected{expectedAcceptance_.run, bLaunchId_, expectedAcceptance_.grant, revision_, 2,
      expectedAcceptance_.host, expectedAcceptance_.core, expectedAcceptance_.intentRevision, expectedAcceptance_.attempt};
    valid = decodeCancelResult(bReadBytes_, expected, result);
    if (valid && result.outcome != CancelOutcome::Failed) cancelTerminal_ = true;
    else valid = false;
  }
  if (!valid) { bProtocolFailed_ = true; return false; }
  bReadBytes_.clear();
  return true;
}

DWORD WindowsBrokerOperations::nextBReadSize() const
{
  if (bReadBytes_.size() < 4) return DWORD(4 - bReadBytes_.size());
  const QByteArray magic = bReadBytes_.left(4);
  const int size = magic == "JGA2" ? 104 : magic == "JRC1" ? 24 : magic == "JCN1" ? 72 : magic == "JCS1" ? 80 : 0;
  return size > bReadBytes_.size() ? DWORD(size - bReadBytes_.size()) : 0;
}

IoProgress WindowsBrokerOperations::takeBSettlement()
{
  const auto settled = bAck_.io->settlement();
  if (!settled || !bReadOperation_ || settled->operation != bReadOperation_ ||
      settled->transferred != settled->bytes.size() || settled->bytes.size() > 104 ||
      bReadBytes_.size() + qsizetype(settled->bytes.size()) > 104) {
    cancelReadAmbiguous_ = true;
    if (!bAck_.io->pending()) bReadPending_ = false;
    return IoProgress::Failed;
  }
  const auto outcome = settled->outcome;
  if (outcome == WindowsRestartIo::Settlement::Outcome::Completed)
    bReadBytes_.append(reinterpret_cast<const char*>(settled->bytes.data()), qsizetype(settled->bytes.size()));
  if (!bAck_.io->consumeSettlement(settled->operation)) { cancelReadAmbiguous_ = true; return IoProgress::Failed; }
  bReadPending_ = false;
  bReadOperation_ = 0;
  if (outcome == WindowsRestartIo::Settlement::Outcome::Completed)
    return parseBBytes() ? IoProgress::Complete : IoProgress::Failed;
  if (outcome == WindowsRestartIo::Settlement::Outcome::Eof) return IoProgress::Eof;
  if (outcome == WindowsRestartIo::Settlement::Outcome::Uncertain) cancelReadAmbiguous_ = true;
  return IoProgress::Failed;
}

IoProgress WindowsBrokerOperations::trackedBRead(DWORD size, qint64 deadline)
{
  if (!size || deadline < 0 || !bAck_.io || !bAck_.channel) return IoProgress::Failed;
  IoProgress progress;
  if (bReadPending_) progress = bAck_.io->poll();
  else {
    bReadPending_ = true;
    progress = bAck_.io->beginTrackedRead(bAck_.channel->parentHandle(), size, ULONGLONG(deadline));
    bReadOperation_ = bAck_.io->operation();
  }
  if (progress == IoProgress::Pending || progress == IoProgress::Unconfirmed) return progress;
  return takeBSettlement();
}

Operations::Progress WindowsBrokerOperations::issueRestorePermit(qint64 deadline, const std::function<bool()>& stillCurrent)
{
  if (permitCallActive_ || !bAccepted_ || !b_ || !peek_ || !stillCurrent ||
      !b_->ownsEndpoint(bAck_.channel, bAck_.io)) return Progress::Failed;
  QScopedValueRollback<bool> guard(permitCallActive_, true);
  if (permitStage_ != 2) {
    DWORD available = 0;
    if (!peek_(bAck_.channel->parentHandle(), nullptr, 0, nullptr, &available, nullptr) || available)
      return Progress::Failed;
  }
  if (permitStage_ == 0) {
    bool allowed = false;
    try { allowed = stillCurrent(); } catch (...) { return Progress::Failed; }
    if (!allowed) return Progress::Failed;
  }
  const RestorePermit permit{expectedAcceptance_.run, expectedAcceptance_.launch, expectedAcceptance_.grant,
                             expectedAcceptance_.revision, 1, expectedAcceptance_.digest};
  return write(bControl_, encodeRestorePermit(permit), permitStage_, deadline);
}

Operations::Progress WindowsBrokerOperations::monitorB(qint64 deadline)
{
  if (permitStage_ != 2 || !b_ || !b_->ownsEndpoint(bAck_.channel, bAck_.io)) return Progress::Failed;
  if (!bEof_) {
    const auto status = trackedBRead(1, deadline);
    if (status == IoProgress::Complete || status == IoProgress::Failed) return Progress::Failed;
    bEof_ = status == IoProgress::Eof;
  }
  const auto process = b_->pollHealthy(ULONGLONG(deadline));
  if (process == ProcessProgress::Failed) return Progress::Failed;
  if (process == ProcessProgress::Complete && bEof_) { bSettled_ = true; return Progress::Complete; }
  return Progress::Pending;
}

Operations::Progress WindowsBrokerOperations::cancelBAndAwaitAck(qint64 deadline)
{
  if (!valid_ || !b_ || revision_ == (std::numeric_limits<quint64>::max)()) return Progress::Failed;
  cancelStarted_ = true;
  if (bProtocolFailed_) cancelReadAmbiguous_ = true;
  if (bIdentityFramePending_) {
    const auto status = b_->cancelHealthyModuleFrame();
    if (status == ProcessProgress::Pending || status == ProcessProgress::Unconfirmed) return Progress::Pending;
    bIdentityFramePending_ = false;
  }
  if (!b_->launchEstablished()) {
    const auto result = b_->forceCleanup(ULONGLONG(deadline));
    return result == ProcessProgress::Pending || result == ProcessProgress::Unconfirmed ? Progress::Pending : Progress::Failed;
  }
  if (!cancelInitialReadHandled_ && bReadPending_) {
    const auto status = bAck_.io->cancel();
    if (status == IoProgress::Pending || status == IoProgress::Unconfirmed || bAck_.io->pending()) return Progress::Pending;
    const auto settled = takeBSettlement();
    if (settled == IoProgress::Eof || settled == IoProgress::Unconfirmed || bProtocolFailed_) cancelReadAmbiguous_ = true;
  }
  cancelInitialReadHandled_ = true;
  if (bControl_.io && bControl_.io->pending()) {
    cancelReadAmbiguous_ = true;
    bControl_.io->cancel();
    if (bControl_.io->pending()) return Progress::Pending;
  }
  if (bGrant_.io && bGrant_.io->pending()) bGrant_.io->cancel();
  if (cancelReadAmbiguous_) {
    if (bCancel_.request.isEmpty()) bCancel_.request = encodeControl({ControlKind::UserStop, 2, revision_ + 1});
    const auto status = write(bControl_, bCancel_.request, bCancel_.stage, deadline);
    return status == Progress::Pending ? status : Progress::Failed;
  }
  if (bCancel_.request.isEmpty()) bCancel_.request = encodeControl({ControlKind::UserStop, 2, revision_ + 1});
  const auto sent = write(bControl_, bCancel_.request, bCancel_.stage, deadline);
  if (sent != Progress::Complete) return sent;
  if (cancelTerminal_) {
    const auto trailing = trackedBRead(1, deadline);
    if (trailing == IoProgress::Pending || trailing == IoProgress::Unconfirmed) return Progress::Pending;
    return trailing == IoProgress::Eof && bReadBytes_.isEmpty() ? Progress::Complete : Progress::Failed;
  }
  const auto status = trackedBRead(nextBReadSize(), deadline);
  if (status == IoProgress::Pending || status == IoProgress::Unconfirmed) return Progress::Pending;
  if (status != IoProgress::Complete) return Progress::Failed;
  return Progress::Pending;
}

Operations::Progress WindowsBrokerOperations::cleanupOwned(qint64 deadline)
{
  if (!valid_) return Progress::Failed; // The caller retains both allocations for separate cleanup.
  for (auto endpoint : {evidence_, aControl_, aAck_, bGrant_, bControl_, bAck_}) {
    if (endpoint.io && endpoint.io->pending()) endpoint.io->cancel();
    if (endpoint.io && endpoint.io->pending()) return Progress::Pending;
  }
  if (a_ && !aSettled_) {
    if (a_->release()) { a_ = nullptr; evidence_ = {}; aControl_ = {}; aAck_ = {}; }
    else {
      const auto result = a_->forceCleanup(ULONGLONG(deadline));
      if (result != ProcessProgress::Complete) return result == ProcessProgress::Failed ? Progress::Failed : Progress::Pending;
      aSettled_ = true;
    }
  }
  if (b_ && bLaunchStarted_ && !bSettled_) {
    if (b_->release()) { b_ = nullptr; bGrant_ = {}; bControl_ = {}; bAck_ = {}; }
    else {
      const auto result = b_->forceCleanup(ULONGLONG(deadline));
      if (result != ProcessProgress::Complete) return result == ProcessProgress::Failed ? Progress::Failed : Progress::Pending;
      bSettled_ = true;
    }
  }
  if (a_ && !a_->release()) return Progress::Pending;
  a_ = nullptr;
  evidence_ = {}; aControl_ = {}; aAck_ = {};
  if (b_ && !b_->release()) return Progress::Pending;
  b_ = nullptr;
  bGrant_ = {}; bControl_ = {}; bAck_ = {};
  return Progress::Complete;
}
}
#endif
