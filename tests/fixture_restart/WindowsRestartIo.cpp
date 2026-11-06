#include "WindowsRestartIo.h"
#ifdef _WIN32
#include <cstring>
#include <limits>

namespace FixtureRestart {
namespace {
struct ActiveCall {
  unsigned& count;
  explicit ActiveCall(unsigned& value) : count(value) { ++count; }
  ~ActiveCall() { --count; }
};
}
WindowsRestartIo::WindowsRestartIo(const WindowsIoApi& api) : api_(api) {}
WindowsRestartIo::~WindowsRestartIo()
{
  if (overlap_.hEvent) api_.closeHandle(overlap_.hEvent);
}

bool WindowsRestartIo::release()
{
  if (owned_ || retainedWork()) return false;
  delete this;
  return true;
}

bool WindowsRestartIo::canBind(HANDLE pipe) const
{
  return !closed_ && !inFlight_ && !activeCalls_ && pipe && pipe != INVALID_HANDLE_VALUE &&
      (bound_ == INVALID_HANDLE_VALUE || bound_ == pipe);
}

bool WindowsRestartIo::bind(HANDLE pipe)
{
  if (!canBind(pipe)) return false;
  bound_ = pipe;
  return true;
}

IoProgress WindowsRestartIo::begin(HANDLE pipe, bool read, const void* data,
                                   DWORD size, ULONGLONG deadline)
{
  if (identityWriter_) return IoProgress::Failed;
  return beginImpl(pipe, read, data, size, deadline);
}

IoProgress WindowsRestartIo::beginTrackedRead(HANDLE pipe, DWORD size, ULONGLONG deadline)
{
  if (identityWriter_) return IoProgress::Failed;
  return beginImpl(pipe, true, nullptr, size, deadline, true);
}

bool WindowsRestartIo::consumeSettlement(std::uint64_t operation)
{
  if (!settlement_ || !operation || settlement_->operation != operation) return false;
  settlement_.reset();
  return true;
}

bool WindowsRestartIo::sealIdentityWriter()
{
  if (!owned_ || closed_ || inFlight_ || identityWriter_) return false;
  identityWriter_ = true;
  return true;
}

IoProgress WindowsRestartIo::beginIdentity(HANDLE pipe, const void* data, DWORD size, ULONGLONG deadline)
{
  return identityWriter_ ? beginImpl(pipe, false, data, size, deadline) : IoProgress::Failed;
}

IoProgress WindowsRestartIo::pollIdentity()
{
  return identityWriter_ ? pollImpl() : IoProgress::Failed;
}

IoProgress WindowsRestartIo::beginImpl(HANDLE pipe, bool read, const void* data,
                                       DWORD size, ULONGLONG deadline, bool tracked)
{
  if (inFlight_ || submitting_ || activeCalls_ || settlement_ || poisoned_) return IoProgress::Failed;
  if (tracked && nextOperation_ == (std::numeric_limits<std::uint64_t>::max)()) return IoProgress::Failed;
  ActiveCall active(activeCalls_);
  if (closed_ || !size || size > 9216 || (!read && !data) ||
      pipe == INVALID_HANDLE_VALUE || !pipe ||
      (bound_ != INVALID_HANDLE_VALUE && pipe != bound_) || api_.now() >= deadline)
    return state_ = IoProgress::Failed;
  if (overlap_.hEvent) api_.closeHandle(overlap_.hEvent);
  overlap_ = {};
  overlap_.hEvent = api_.createEvent(nullptr, TRUE, FALSE, nullptr);
  if (!overlap_.hEvent) return state_ = IoProgress::Failed;
  pipe_ = pipe;
  bytes_.assign(size, 0);
  if (!read) std::memcpy(bytes_.data(), data, size);
  offset_ = 0;
  deadline_ = deadline;
  read_ = read;
  tracked_ = tracked;
  operation_ = tracked ? ++nextOperation_ : 0;
  cancelling_ = false;
  cancelRequested_ = false;
  return submit();
}

IoProgress WindowsRestartIo::submit()
{
  while (offset_ < bytes_.size()) {
    if (api_.now() >= deadline_ || !api_.resetEvent(overlap_.hEvent)) {
      if (tracked_) return settleTracked(Settlement::Outcome::Uncertain);
      return state_ = IoProgress::Failed;
    }
    HANDLE event = overlap_.hEvent;
    overlap_ = {};
    overlap_.hEvent = event;
    DWORD count = 0;
    const DWORD remaining = static_cast<DWORD>(bytes_.size()) - offset_;
    inFlight_ = true;
    submitting_ = true;
    const BOOL ok = read_ ? api_.readFile(pipe_, bytes_.data() + offset_, remaining,
                                          &count, &overlap_)
                          : api_.writeFile(pipe_, bytes_.data() + offset_, remaining,
                                           &count, &overlap_);
    submitting_ = false;
    if (!inFlight_) return state_; // A reentrant completion already settled this operation.
    if (!ok) {
      const DWORD error = api_.error();
      if (error != ERROR_IO_PENDING) {
        inFlight_ = false;
        if (tracked_ && count) return settleTracked(Settlement::Outcome::Uncertain);
        return completedError(error);
      }
      return state_ = IoProgress::Pending;
    }
    inFlight_ = false;
    if (tracked_) {
      if (!count || count > remaining) return settleTracked(Settlement::Outcome::Uncertain);
      offset_ += count;
      return settleTracked(Settlement::Outcome::Completed);
    }
    if (api_.now() >= deadline_ || !count || count > remaining)
      return state_ = IoProgress::Failed;
    offset_ += count;
  }
  return state_ = IoProgress::Complete;
}

IoProgress WindowsRestartIo::completedError(DWORD error)
{
  if (tracked_) {
    if (!cancelling_ && offset_ == 0 && (error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA))
      return settleTracked(Settlement::Outcome::Eof);
    return settleTracked(Settlement::Outcome::Uncertain);
  }
  if (read_ && !cancelling_ && offset_ == 0 && api_.now() < deadline_ &&
      (error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA))
    return state_ = IoProgress::Eof;
  return state_ = IoProgress::Failed;
}

IoProgress WindowsRestartIo::completion(DWORD count)
{
  inFlight_ = false;
  if (tracked_) {
    if (!count || count > bytes_.size() - offset_) return settleTracked(Settlement::Outcome::Uncertain);
    offset_ += count;
    return settleTracked(Settlement::Outcome::Completed);
  }
  if (cancelling_ || api_.now() >= deadline_ || !count || count > bytes_.size() - offset_)
    return state_ = IoProgress::Failed;
  offset_ += count;
  if (offset_ == bytes_.size()) return state_ = IoProgress::Complete;
  return submit();
}

IoProgress WindowsRestartIo::settleTracked(Settlement::Outcome outcome)
{
  try {
    auto value = std::make_shared<Settlement>();
    value->operation = operation_;
    value->outcome = outcome;
    value->transferred = offset_;
    value->bytes.assign(bytes_.begin(), bytes_.begin() + offset_);
    settlement_ = std::move(value);
  } catch (...) {
    poisoned_ = true;
    return state_ = IoProgress::Failed;
  }
  if (outcome == Settlement::Outcome::Uncertain) poisoned_ = true;
  if (outcome == Settlement::Outcome::Completed) return state_ = IoProgress::Complete;
  if (outcome == Settlement::Outcome::Eof) return state_ = IoProgress::Eof;
  return state_ = IoProgress::Failed;
}

IoProgress WindowsRestartIo::poll()
{
  return identityWriter_ ? IoProgress::Failed : pollImpl();
}

IoProgress WindowsRestartIo::pollImpl()
{
  ActiveCall active(activeCalls_);
  if (!inFlight_) return state_;
  DWORD count = tracked_ ? MAXDWORD : 0;
  const BOOL completed = api_.result(pipe_, &overlap_, &count, FALSE);
  if (!inFlight_) return state_;
  if (completed) return completion(count);
  const DWORD error = api_.error();
  if (error == ERROR_IO_INCOMPLETE) {
    if (api_.now() >= deadline_) return cancelImpl();
    return IoProgress::Pending;
  }
  if (error != ERROR_OPERATION_ABORTED && error != ERROR_BROKEN_PIPE &&
      error != ERROR_NO_DATA && error != ERROR_PIPE_NOT_CONNECTED)
    return IoProgress::Unconfirmed;
  inFlight_ = false;
  if (tracked_ && error == ERROR_OPERATION_ABORTED)
    return count == 0 ? settleTracked(Settlement::Outcome::Aborted)
                      : settleTracked(Settlement::Outcome::Uncertain);
  if (tracked_ && count != 0) return settleTracked(Settlement::Outcome::Uncertain);
  return completedError(error);
}

IoProgress WindowsRestartIo::cancel()
{
  return identityWriter_ ? IoProgress::Failed : cancelImpl();
}

IoProgress WindowsRestartIo::cancelImpl()
{
  ActiveCall active(activeCalls_);
  if (!inFlight_) return state_;
  cancelling_ = true;
  if (!cancelRequested_) {
    cancelRequested_ = true;
    (void)api_.cancel(pipe_, &overlap_);
    if (!inFlight_) return state_;
  }
  DWORD count = tracked_ ? MAXDWORD : 0;
  const BOOL completed = api_.result(pipe_, &overlap_, &count, FALSE);
  if (!inFlight_) return state_;
  if (completed) return completion(count);
  const DWORD error = api_.error();
  if (error != ERROR_OPERATION_ABORTED && error != ERROR_BROKEN_PIPE &&
      error != ERROR_NO_DATA && error != ERROR_PIPE_NOT_CONNECTED)
    return IoProgress::Unconfirmed;
  inFlight_ = false;
  if (tracked_ && error == ERROR_OPERATION_ABORTED)
    return count == 0 ? settleTracked(Settlement::Outcome::Aborted)
                      : settleTracked(Settlement::Outcome::Uncertain);
  if (tracked_) return settleTracked(Settlement::Outcome::Uncertain);
  return state_ = IoProgress::Failed;
}
}
#endif
