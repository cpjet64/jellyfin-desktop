#include "WindowsRestartIo.h"
#ifdef _WIN32
#include <cstring>

namespace FixtureRestart {
WindowsRestartIo::WindowsRestartIo(const WindowsIoApi& api) : api_(api) {}
WindowsRestartIo::~WindowsRestartIo()
{
  if (overlap_.hEvent) api_.closeHandle(overlap_.hEvent);
}

bool WindowsRestartIo::release()
{
  if (inFlight_ || owned_) return false;
  delete this;
  return true;
}

bool WindowsRestartIo::canBind(HANDLE pipe) const
{
  return !inFlight_ && pipe && pipe != INVALID_HANDLE_VALUE &&
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
  if (inFlight_ || !size || size > 9216 || (!read && !data) ||
      pipe == INVALID_HANDLE_VALUE || !pipe ||
      (bound_ != INVALID_HANDLE_VALUE && pipe != bound_) || api_.now() >= deadline)
    return IoProgress::Failed;
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
  cancelling_ = false;
  return submit();
}

IoProgress WindowsRestartIo::submit()
{
  while (offset_ < bytes_.size()) {
    if (api_.now() >= deadline_ || !api_.resetEvent(overlap_.hEvent))
      return state_ = IoProgress::Failed;
    HANDLE event = overlap_.hEvent;
    overlap_ = {};
    overlap_.hEvent = event;
    DWORD count = 0;
    const DWORD remaining = static_cast<DWORD>(bytes_.size()) - offset_;
    const BOOL ok = read_ ? api_.readFile(pipe_, bytes_.data() + offset_, remaining,
                                          &count, &overlap_)
                          : api_.writeFile(pipe_, bytes_.data() + offset_, remaining,
                                           &count, &overlap_);
    if (!ok) {
      if (api_.error() != ERROR_IO_PENDING) return state_ = IoProgress::Failed;
      inFlight_ = true;
      return state_ = IoProgress::Pending;
    }
    if (api_.now() >= deadline_ || !count || count > remaining)
      return state_ = IoProgress::Failed;
    offset_ += count;
  }
  return state_ = IoProgress::Complete;
}

IoProgress WindowsRestartIo::completion(DWORD count)
{
  inFlight_ = false;
  if (cancelling_ || api_.now() >= deadline_ || !count || count > bytes_.size() - offset_)
    return state_ = IoProgress::Failed;
  offset_ += count;
  if (offset_ == bytes_.size()) return state_ = IoProgress::Complete;
  return submit();
}

IoProgress WindowsRestartIo::poll()
{
  if (!inFlight_) return state_;
  DWORD count = 0;
  if (api_.result(pipe_, &overlap_, &count, FALSE)) return completion(count);
  const DWORD error = api_.error();
  if (error == ERROR_IO_INCOMPLETE) {
    if (api_.now() >= deadline_) return cancel();
    return IoProgress::Pending;
  }
  if (error != ERROR_OPERATION_ABORTED && error != ERROR_BROKEN_PIPE &&
      error != ERROR_NO_DATA && error != ERROR_PIPE_NOT_CONNECTED)
    return IoProgress::Unconfirmed;
  inFlight_ = false;
  return state_ = IoProgress::Failed;
}

IoProgress WindowsRestartIo::cancel()
{
  if (!inFlight_) return state_;
  cancelling_ = true;
  if (!api_.cancel(pipe_, &overlap_) && api_.error() != ERROR_NOT_FOUND)
    return IoProgress::Unconfirmed;
  DWORD count = 0;
  if (api_.result(pipe_, &overlap_, &count, FALSE)) return completion(count);
  const DWORD error = api_.error();
  if (error != ERROR_OPERATION_ABORTED && error != ERROR_BROKEN_PIPE &&
      error != ERROR_NO_DATA && error != ERROR_PIPE_NOT_CONNECTED)
    return IoProgress::Unconfirmed;
  inFlight_ = false;
  return state_ = IoProgress::Failed;
}
}
#endif
