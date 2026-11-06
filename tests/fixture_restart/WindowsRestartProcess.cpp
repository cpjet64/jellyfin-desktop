#include "WindowsRestartProcess.h"
#ifdef _WIN32
#include <algorithm>
#include <cstddef>
#include <utility>

namespace FixtureRestart {
WindowsRestartProcess::WindowsRestartProcess(const WindowsProcessApi& api,
    std::vector<HANDLE> handles, std::vector<WindowsRestartIo*> io)
  : api_(api), handles_(std::move(handles)), io_(std::move(io)) {}

WindowsRestartProcess* WindowsRestartProcess::create(const WindowsProcessApi& api,
    std::vector<HANDLE> handles, std::vector<WindowsRestartIo*> io)
{
  if (handles.empty() || handles.size() > 4) return nullptr;
  for (size_t i = 0; i < handles.size(); ++i) {
    if (!handles[i] || handles[i] == INVALID_HANDLE_VALUE ||
        std::find(handles.begin(), handles.begin() + i, handles[i]) != handles.begin() + i)
      return nullptr;
  }
  for (size_t i = 0; i < io.size(); ++i) {
    if (!io[i] || std::find(io.begin(), io.begin() + i, io[i]) != io.begin() + i)
      return nullptr;
  }
  return new WindowsRestartProcess(api, std::move(handles), std::move(io));
}

void WindowsRestartProcess::closeChildren()
{
  for (HANDLE handle : handles_) api_.close(handle);
  handles_.clear();
}

WindowsRestartProcess::~WindowsRestartProcess()
{
  closeChildren();
  for (auto* item : io_) item->release();
  if (process_) api_.close(process_);
  if (job_) api_.close(job_);
}

bool WindowsRestartProcess::release()
{
  if (!settled_) return false;
  for (auto* item : io_) if (item->pending()) return false;
  delete this;
  return true;
}

ProcessProgress WindowsRestartProcess::launch(const std::wstring& executable,
    const std::wstring& arguments, ULONGLONG deadline)
{
  if (launched_ || api_.now() >= deadline || executable.size() < 3 ||
      executable[1] != L':' || (executable[2] != L'\\' && executable[2] != L'/') ||
      executable.find_first_of(L"\"\0", 0, 2) != std::wstring::npos ||
      arguments.find(L'\0') != std::wstring::npos) return ProcessProgress::Failed;
  launched_ = true;
  job_ = api_.createJob(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!job_ || !api_.setJob(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
    return ProcessProgress::Failed;
  SIZE_T bytes = 0;
  api_.initAttributes(nullptr, 2, 0, &bytes);
  if (!bytes) return ProcessProgress::Failed;
  std::vector<std::max_align_t> storage((bytes + sizeof(std::max_align_t) - 1) /
                                        sizeof(std::max_align_t));
  auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
  if (!api_.initAttributes(attributes, 2, 0, &bytes)) return ProcessProgress::Failed;
  const bool ready = api_.updateAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
      handles_.data(), handles_.size() * sizeof(HANDLE), nullptr, nullptr) &&
      api_.updateAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST,
      &job_, sizeof(job_), nullptr, nullptr);
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.lpAttributeList = attributes;
  PROCESS_INFORMATION process{};
  std::wstring command = L"\"" + executable + L"\" " + arguments;
  const bool created = ready && api_.now() < deadline && api_.createProcess(executable.c_str(),
      command.data(), nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED |
      CREATE_NO_WINDOW, nullptr, nullptr, &startup.StartupInfo, &process);
  api_.deleteAttributes(attributes);
  if (!created) return ProcessProgress::Failed;
  process_ = process.hProcess;
  settled_ = false;
  closeChildren();
  BOOL inJob = FALSE;
  assigned_ = api_.inJob(process_, job_, &inJob) && inJob;
  const bool resumed = assigned_ && api_.now() < deadline && api_.resume(process.hThread) != DWORD(-1);
  api_.close(process.hThread);
  if (!resumed || api_.now() >= deadline) {
    forced_ = true;
    return forceCleanup(deadline) == ProcessProgress::Complete
        ? ProcessProgress::Failed : ProcessProgress::Unconfirmed;
  }
  return ProcessProgress::Complete;
}

ProcessProgress WindowsRestartProcess::settle(ULONGLONG deadline, bool healthy)
{
  if (!process_ || !job_) return ProcessProgress::Failed;
  const DWORD wait = api_.wait(process_, 0);
  JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
  if (wait == WAIT_FAILED || !api_.queryJob(job_, JobObjectBasicAccountingInformation,
                                             &info, sizeof(info), nullptr))
    return ProcessProgress::Unconfirmed;
  if (wait != WAIT_OBJECT_0 || info.ActiveProcesses != 0)
    return api_.now() >= deadline ? ProcessProgress::Unconfirmed : ProcessProgress::Pending;
  DWORD exit = STILL_ACTIVE;
  if (!api_.exitCode(process_, &exit) || exit == STILL_ACTIVE) return ProcessProgress::Unconfirmed;
  settled_ = true;
  if (api_.now() >= deadline || (healthy && (forced_ || !assigned_ || exit != 0)))
    return ProcessProgress::Failed;
  return ProcessProgress::Complete;
}

ProcessProgress WindowsRestartProcess::pollHealthy(ULONGLONG deadline)
{
  if (forced_) return ProcessProgress::Failed;
  return settle(deadline, true);
}

ProcessProgress WindowsRestartProcess::forceCleanup(ULONGLONG deadline)
{
  forced_ = true;
  if (!process_ || !job_) return ProcessProgress::Failed;
  if (!terminationRequested_) {
    terminationRequested_ = true;
    const bool jobStopped = api_.terminateJob(job_, 1) != FALSE;
    const bool processStopped = assigned_ || api_.terminateProcess(process_, 1) != FALSE;
    if (!jobStopped || !processStopped) return ProcessProgress::Unconfirmed;
  }
  return settle(deadline, false);
}
}
#endif
