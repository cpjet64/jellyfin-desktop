#pragma once
#ifdef _WIN32
#include "WindowsRestartIo.h"
#include <windows.h>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace FixtureRestart {
class WindowsRestartChannel;
enum class ProcessProgress { Pending, Complete, Failed, Unconfirmed };
struct WindowsProcessApi {
  decltype(&CreateJobObjectW) createJob = &CreateJobObjectW;
  decltype(&SetInformationJobObject) setJob = &SetInformationJobObject;
  decltype(&InitializeProcThreadAttributeList) initAttributes = &InitializeProcThreadAttributeList;
  decltype(&UpdateProcThreadAttribute) updateAttribute = &UpdateProcThreadAttribute;
  decltype(&DeleteProcThreadAttributeList) deleteAttributes = &DeleteProcThreadAttributeList;
  decltype(&CreateProcessW) createProcess = &CreateProcessW;
  decltype(&IsProcessInJob) inJob = &IsProcessInJob;
  decltype(&ResumeThread) resume = &ResumeThread;
  decltype(&TerminateJobObject) terminateJob = &TerminateJobObject;
  decltype(&TerminateProcess) terminateProcess = &TerminateProcess;
  decltype(&WaitForSingleObject) wait = &WaitForSingleObject;
  decltype(&QueryInformationJobObject) queryJob = &QueryInformationJobObject;
  decltype(&GetExitCodeProcess) exitCode = &GetExitCodeProcess;
  decltype(&CloseHandle) close = &CloseHandle;
  decltype(&GetTickCount64) now = &GetTickCount64;
};

// Owns the supplied child handles and I/O objects after successful creation.
// Pipe construction and connection are a separate component.
class WindowsRestartProcess {
public:
  static WindowsRestartProcess* create(const WindowsProcessApi& api,
      std::vector<HANDLE> childHandles, std::vector<WindowsRestartIo*> io);
  static WindowsRestartProcess* createWithChannels(const WindowsProcessApi& api,
      const std::vector<WindowsRestartChannel*>& channels, std::vector<WindowsRestartIo*> io);
  bool release();
  bool ownsChannel(const WindowsRestartChannel* channel) const;
  bool ownsEndpoint(const WindowsRestartChannel* channel, const WindowsRestartIo* io) const;
  bool launchEstablished() const { return established_ && process_ && assigned_; }
  bool closeParentChannel(WindowsRestartChannel* channel);
  ProcessProgress launch(const std::wstring& executable,
      const std::wstring& arguments, ULONGLONG deadline);
  ProcessProgress pollLaunch();
  ProcessProgress pollRunning() const;
  ProcessProgress pollHealthy(ULONGLONG deadline);
  ProcessProgress forceCleanup(ULONGLONG deadline);
private:
  WindowsRestartProcess(const WindowsProcessApi& api, std::vector<HANDLE> handles,
                        std::vector<WindowsRestartIo*> io);
  ~WindowsRestartProcess();
  WindowsRestartProcess(const WindowsRestartProcess&) = delete;
  WindowsRestartProcess& operator=(const WindowsRestartProcess&) = delete;
  ProcessProgress settle(ULONGLONG deadline, bool healthy);
  void runCreate(std::wstring executable, std::wstring arguments, ULONGLONG deadline) noexcept;
  void closeChildren();
  WindowsProcessApi api_;
  std::vector<HANDLE> handles_;
  std::vector<WindowsRestartIo*> io_;
  std::vector<WindowsRestartChannel*> channels_;
  HANDLE job_ = nullptr, process_ = nullptr;
  std::thread worker_;
  std::atomic<bool> workerDone_{false};
  PROCESS_INFORMATION workerResult_{};
  ULONGLONG launchDeadline_ = 0;
  bool workerCreated_ = false;
  bool assigned_ = false, forced_ = false, terminationRequested_ = false;
  bool settled_ = true, launched_ = false, established_ = false;
};
}
#endif
