#pragma once
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>

namespace FixtureRestart {
enum class ChannelProgress { Pending, Complete, Failed, Unconfirmed };
struct WindowsChannelApi {
  decltype(&OpenProcessToken) openToken = &OpenProcessToken;
  decltype(&GetCurrentProcess) currentProcess = &GetCurrentProcess;
  decltype(&GetTokenInformation) tokenInfo = &GetTokenInformation;
  decltype(&ConvertSidToStringSidW) sidString = &ConvertSidToStringSidW;
  decltype(&ConvertStringSecurityDescriptorToSecurityDescriptorW) descriptor = &ConvertStringSecurityDescriptorToSecurityDescriptorW;
  decltype(&LocalFree) localFree = &LocalFree;
  decltype(&BCryptGenRandom) random = &BCryptGenRandom;
  decltype(&CreateNamedPipeW) createPipe = &CreateNamedPipeW;
  decltype(&CreateFileW) createFile = &CreateFileW;
  decltype(&CreateEventW) event = &CreateEventW;
  decltype(&ConnectNamedPipe) connect = &ConnectNamedPipe;
  decltype(&GetOverlappedResult) result = &GetOverlappedResult;
  decltype(&CancelIoEx) cancel = &CancelIoEx;
  decltype(&CloseHandle) close = &CloseHandle;
  decltype(&GetLastError) error = &GetLastError;
  decltype(&GetTickCount64) now = &GetTickCount64;
};
class WindowsRestartProcess;
class WindowsRestartChannel {
public:
  static WindowsRestartChannel* create(const WindowsChannelApi& api);
  ChannelProgress begin(ULONGLONG deadline);
  ChannelProgress poll();
  ChannelProgress cancel();
  HANDLE parentHandle() const { return connected_ ? server_ : INVALID_HANDLE_VALUE; }
  HANDLE childHandle() const { return connected_ ? child_ : INVALID_HANDLE_VALUE; }
  bool release();
private:
  friend class WindowsRestartProcess;
  explicit WindowsRestartChannel(const WindowsChannelApi& api) : api_(api) {}
  ~WindowsRestartChannel();
  PSECURITY_DESCRIPTOR security();
  WindowsChannelApi api_;
  HANDLE server_ = INVALID_HANDLE_VALUE, child_ = INVALID_HANDLE_VALUE;
  OVERLAPPED connection_{};
  ULONGLONG deadline_ = 0;
  bool started_ = false, pending_ = false, connected_ = false;
  bool cancelling_ = false, owned_ = false;
  ChannelProgress state_ = ChannelProgress::Failed;
};
}
#endif
