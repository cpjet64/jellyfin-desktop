#pragma once

// The API object supplies thread-mode and loader calls; it does not allocate.
template <typename Api>
class ThreadErrorModeGuard
{
public:
  explicit ThreadErrorModeGuard(Api &api) noexcept : api_(api) {}
  bool enter(unsigned long requested) noexcept
  {
    active_ = api_.setMode(requested, &previous_);
    return active_;
  }
  bool restore() noexcept
  {
    if (!active_) return true;
    active_ = false;
    return api_.setMode(previous_, nullptr);
  }
  ~ThreadErrorModeGuard() noexcept { restore(); }

private:
  Api &api_;
  unsigned long previous_ = 0;
  bool active_ = false;
};

template <typename Handle>
struct LoaderModeResult
{
  Handle module = nullptr;
  unsigned long error = 0;
  unsigned long restoreError = 0;
  bool entered = false;
  bool restored = false;
};

template <typename Api>
LoaderModeResult<typename Api::Handle> loadWithThreadErrorMode(Api &api, const wchar_t *path)
{
  LoaderModeResult<typename Api::Handle> result;
  ThreadErrorModeGuard<Api> guard(api);
  const auto requested = api.mode() | api.failCriticalErrors();
  if (!guard.enter(requested)) {
    result.error = api.lastError();
    return result;
  }
  result.entered = true;
  result.module = api.load(path);
  if (!result.module) result.error = api.lastError();
  result.restored = guard.restore();
  if (!result.restored) result.restoreError = api.lastError();
  return result;
}
