#include "WindowsBrokerOperations.h"
#ifdef _WIN32
#include <limits>
#include <utility>
#include <array>

namespace FixtureRestart {
WindowsBrokerOperations::WindowsBrokerOperations(WindowsRestartProcess& a, WindowsRestartProcess& b,
    WindowsEndpoint evidence, WindowsEndpoint aControl, WindowsEndpoint aAck, WindowsEndpoint bGrant,
    WindowsEndpoint bControl, WindowsEndpoint bAck, std::wstring executable, std::wstring arguments, quint64 revision)
  : a_(&a), b_(&b), evidence_(evidence), aControl_(aControl), aAck_(aAck), bGrant_(bGrant),
    bControl_(bControl), bAck_(bAck), executable_(std::move(executable)), arguments_(std::move(arguments)), revision_(revision)
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

Operations::Progress WindowsBrokerOperations::waitAAndChildren(qint64 deadline, bool& forced)
{
  forced = false;
  if (!valid_ || !a_) return Progress::Failed;
  const auto result = a_->pollHealthy(ULONGLONG(deadline));
  if (result == ProcessProgress::Complete) { aSettled_ = true; return Progress::Complete; }
  return result == ProcessProgress::Pending || result == ProcessProgress::Unconfirmed ? Progress::Pending : Progress::Failed;
}

Operations::Progress WindowsBrokerOperations::launchB(qint64 deadline)
{
  if (!valid_ || !aSettled_ || !b_) return Progress::Failed;
  const auto result = bLaunchStarted_ ? b_->pollLaunch() : b_->launch(executable_, arguments_, ULONGLONG(deadline));
  bLaunchStarted_ = true;
  if (result == ProcessProgress::Complete) return Progress::Complete;
  return result == ProcessProgress::Pending || result == ProcessProgress::Unconfirmed ? Progress::Pending : Progress::Failed;
}

Operations::Progress WindowsBrokerOperations::transferToB(const QByteArray& frame, qint64 deadline)
{
  if (!valid_ || !aSettled_ || !b_ || !b_->launchEstablished()) return Progress::Failed;
  const auto result = write(bGrant_, frame, grantStage_, deadline);
  if (result != Progress::Complete) return result;
  return b_->closeParentChannel(bGrant_.channel) ? Progress::Complete : Progress::Failed;
}

Operations::Progress WindowsBrokerOperations::cancelBAndAwaitAck(qint64 deadline)
{
  if (!valid_ || !b_ || revision_ == (std::numeric_limits<quint64>::max)()) return Progress::Failed;
  if (!b_->launchEstablished()) {
    const auto result = b_->forceCleanup(ULONGLONG(deadline));
    return result == ProcessProgress::Pending || result == ProcessProgress::Unconfirmed ? Progress::Pending : Progress::Failed;
  }
  if (bGrant_.io && bGrant_.io->pending()) bGrant_.io->cancel();
  return exchange(bCancel_, bControl_, bAck_, {ControlKind::UserStop, 2, revision_ + 1}, deadline);
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
