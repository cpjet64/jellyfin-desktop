#pragma once
#ifdef _WIN32
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <functional>
#include <vector>

namespace FixtureRestart {
class WindowsRestartProcess;
enum class IoProgress { Pending, Complete, Eof, Failed, Unconfirmed };

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
  struct Settlement {
    enum class Outcome { Completed, Aborted, Eof, Uncertain };
    std::uint64_t operation = 0;
    Outcome outcome = Outcome::Uncertain;
    DWORD transferred = 0;
    DWORD requested = 0;
    bool read = false;
    std::vector<unsigned char> bytes;
  };
  explicit WindowsRestartIo(const WindowsIoApi& api);
  bool release();
  WindowsRestartIo(const WindowsRestartIo&) = delete;
  WindowsRestartIo& operator=(const WindowsRestartIo&) = delete;
  IoProgress begin(HANDLE pipe, bool read, const void* data, DWORD size,
                   ULONGLONG deadline);
  IoProgress beginGuarded(HANDLE pipe, bool read, const void* data, DWORD size,
                          ULONGLONG deadline, const std::function<bool()>& authority);
  IoProgress beginTrackedRead(HANDLE pipe, DWORD size, ULONGLONG deadline);
  std::uint64_t operation() const { return operation_; }
  std::shared_ptr<const Settlement> settlement() const { return settlement_; }
  bool consumeSettlement(std::uint64_t operation);
  IoProgress poll();
  IoProgress cancel();
  IoProgress pollSettlementOnly();
  IoProgress cancelSettlementOnly();
  IoProgress pollGuarded(const std::function<bool()>& authority);
  IoProgress cancelGuarded(const std::function<bool()>& authority);
  bool canBind(HANDLE pipe) const;
  bool bind(HANDLE pipe);
  bool pending() const { return inFlight_; }
  bool canReleaseStorage() const { return !retainedWork(); }
  bool canceledWithoutData() const { return confirmedEmptyCancellation_ && canReleaseStorage(); }
  const std::vector<unsigned char>& bytes() const { return bytes_; }
private:
  friend class WindowsRestartProcess;
  bool retainedWork() const { return inFlight_ || submitting_ || activeCalls_ || settlement_; }
  ~WindowsRestartIo();
  bool sealIdentityWriter();
  IoProgress beginIdentity(HANDLE pipe, const void* data, DWORD size, ULONGLONG deadline);
  IoProgress pollIdentity();
  IoProgress beginImpl(HANDLE pipe, bool read, const void* data, DWORD size, ULONGLONG deadline,
                       bool tracked = false);
  IoProgress pollImpl();
  IoProgress cancelImpl();
  IoProgress submit();
  IoProgress completion(DWORD count);
  IoProgress completedError(DWORD error);
  IoProgress settleTracked(Settlement::Outcome outcome);
  bool latchSettlementOnly();
  bool continuationAllowed() const noexcept;
  WindowsIoApi api_;
  OVERLAPPED overlap_{};
  HANDLE pipe_ = INVALID_HANDLE_VALUE;
  HANDLE bound_ = INVALID_HANDLE_VALUE;
  std::vector<unsigned char> bytes_;
  DWORD offset_ = 0;
  ULONGLONG deadline_ = 0;
  bool read_ = false, inFlight_ = false, cancelling_ = false;
  bool owned_ = false, closed_ = false;
  bool identityWriter_ = false;
  bool tracked_ = false, poisoned_ = false, cancelRequested_ = false, submitting_ = false;
  bool settlementOnly_ = false;
  bool confirmedEmptyCancellation_ = false;
  unsigned activeCalls_ = 0;
  std::uint64_t nextOperation_ = 0, operation_ = 0;
  std::shared_ptr<const Settlement> settlement_;
  const std::function<bool()>* continuationGuard_ = nullptr;
  IoProgress state_ = IoProgress::Failed;
};
}
#endif
