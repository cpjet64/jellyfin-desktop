#pragma once
#ifdef _WIN32
#include <windows.h>
#include <cstddef>
#include <vector>

namespace FixtureRestart {
enum class IoProgress { Pending, Complete, Failed, Unconfirmed };

// The API is injectable so that completion and cancellation races can be
// exercised without creating a process or opening a pipe.
struct WindowsIoApi {
  decltype(&CreateEventW) createEvent = &CreateEventW;
  decltype(&CloseHandle) closeHandle = &CloseHandle;
  decltype(&ResetEvent) resetEvent = &ResetEvent;
  decltype(&ReadFile) readFile = &ReadFile;
  decltype(&WriteFile) writeFile = &WriteFile;
  decltype(&GetOverlappedResult) result = &GetOverlappedResult;
  decltype(&CancelIoEx) cancel = &CancelIoEx;
  decltype(&GetLastError) error = &GetLastError;
  decltype(&GetTickCount64) now = &GetTickCount64;
};

// Owns both the OVERLAPPED and its backing bytes until completion is observed.
// A caller must not release the containing allocation while pending() is true.
class WindowsRestartIo {
public:
  explicit WindowsRestartIo(const WindowsIoApi& api);
  bool release();
  WindowsRestartIo(const WindowsRestartIo&) = delete;
  WindowsRestartIo& operator=(const WindowsRestartIo&) = delete;
  IoProgress begin(HANDLE pipe, bool read, const void* data, DWORD size,
                   ULONGLONG deadline);
  IoProgress poll();
  IoProgress cancel();
  bool pending() const { return inFlight_; }
  const std::vector<unsigned char>& bytes() const { return bytes_; }
private:
  ~WindowsRestartIo();
  IoProgress submit();
  IoProgress completion(DWORD count);
  WindowsIoApi api_;
  OVERLAPPED overlap_{};
  HANDLE pipe_ = INVALID_HANDLE_VALUE;
  std::vector<unsigned char> bytes_;
  DWORD offset_ = 0;
  ULONGLONG deadline_ = 0;
  bool read_ = false, inFlight_ = false, cancelling_ = false;
  IoProgress state_ = IoProgress::Failed;
};
}
#endif
