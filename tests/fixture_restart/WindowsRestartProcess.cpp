#include "WindowsRestartProcess.h"
#include "WindowsRestartChannel.h"
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
    if (!io[i] || io[i]->owned_ || std::find(io.begin(), io.begin() + i, io[i]) != io.begin() + i)
      return nullptr;
  }
  auto* process = new WindowsRestartProcess(api, std::move(handles), std::move(io));
  for (auto* item : process->io_) item->owned_ = true;
  return process;
}

WindowsRestartProcess* WindowsRestartProcess::createWithChannels(const WindowsProcessApi& api,
    const std::vector<WindowsRestartChannel*>& channels, std::vector<WindowsRestartIo*> io)
{
  std::vector<HANDLE> children;
  std::vector<HANDLE> parents;
  if (channels.size() != io.size()) return nullptr;
  size_t index = 0;
  for (auto* channel : channels) {
    if (!channel || channel->owned_ || !channel->connected_ || channel->pending_ ||
        channel->child_ == INVALID_HANDLE_VALUE || !io[index] ||
        !io[index]->canBind(channel->server_) ||
        std::find(parents.begin(), parents.end(), channel->server_) != parents.end() ||
        std::find(children.begin(), children.end(), channel->child_) != children.end())
      return nullptr;
    children.push_back(channel->child_);
    parents.push_back(channel->server_);
    ++index;
  }
  auto* process = create(api, std::move(children), std::move(io));
  if (!process) return nullptr;
  process->channels_ = channels;
  index = 0;
  for (auto* channel : channels) {
    process->io_[index++]->bind(channel->server_);
    channel->owned_ = true;
    channel->child_ = INVALID_HANDLE_VALUE;
  }
  return process;
}

void WindowsRestartProcess::closeChildren()
{
  for (HANDLE handle : handles_) api_.close(handle);
  handles_.clear();
}

WindowsRestartProcess::~WindowsRestartProcess()
{
  closeChildren();
  for (auto* item : io_) { item->owned_ = false; item->release(); }
  for (auto* channel : channels_) { channel->owned_ = false; channel->release(); }
  if (process_) api_.close(process_);
  if (job_) api_.close(job_);
}

bool WindowsRestartProcess::release()
{
  if (!settled_ || worker_.joinable()) return false;
  for (auto* item : io_) if (item->pending()) return false;
  delete this;
  return true;
}

bool WindowsRestartProcess::ownsChannel(const WindowsRestartChannel* channel) const
{
  return channel && std::find(channels_.begin(), channels_.end(), channel) != channels_.end();
}

bool WindowsRestartProcess::closeParentChannel(WindowsRestartChannel* channel)
{
  const auto found = std::find(channels_.begin(), channels_.end(), channel);
  if (!channel || found == channels_.end() || !launchEstablished() || worker_.joinable())
    return false;
  const size_t index = static_cast<size_t>(found - channels_.begin());
  auto* io = io_[index];
  if (channel->server_ == INVALID_HANDLE_VALUE || io->bound_ != channel->server_ ||
      io->closed_ || io->inFlight_ || io->read_ || io->state_ != IoProgress::Complete)
    return false;
  if (!channel->api_.close(channel->server_)) return false;
  channel->server_ = INVALID_HANDLE_VALUE;
  io->closed_ = true;
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
  launchDeadline_ = deadline;
  settled_ = false;
  try {
    worker_ = std::thread(&WindowsRestartProcess::runCreate, this, executable, arguments, deadline);
  } catch (...) {
    settled_ = true;
    return ProcessProgress::Failed;
  }
  return ProcessProgress::Pending;
}

void WindowsRestartProcess::runCreate(std::wstring executable, std::wstring arguments,
                                     ULONGLONG deadline) noexcept
{
  try {
  std::wstring command = L"\"" + executable + L"\" " + arguments;
  SIZE_T bytes = 0;
  api_.initAttributes(nullptr, 2, 0, &bytes);
  if (!bytes) { workerDone_.store(true, std::memory_order_release); return; }
  std::vector<std::max_align_t> storage((bytes + sizeof(std::max_align_t) - 1) /
                                        sizeof(std::max_align_t));
  auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
  if (!api_.initAttributes(attributes, 2, 0, &bytes)) {
    workerDone_.store(true, std::memory_order_release); return;
  }
  const bool ready = api_.updateAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
      handles_.data(), handles_.size() * sizeof(HANDLE), nullptr, nullptr) &&
      api_.updateAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST,
      &job_, sizeof(job_), nullptr, nullptr);
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.lpAttributeList = attributes;
  workerCreated_ = ready && api_.now() < deadline && api_.createProcess(executable.c_str(),
      command.data(), nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED |
      CREATE_NO_WINDOW, nullptr, nullptr, &startup.StartupInfo, &workerResult_);
  api_.deleteAttributes(attributes);
  } catch (...) {}
  workerDone_.store(true, std::memory_order_release);
}

ProcessProgress WindowsRestartProcess::pollLaunch()
{
  if (!worker_.joinable()) return ProcessProgress::Failed;
  if (!workerDone_.load(std::memory_order_acquire))
    return api_.now() >= launchDeadline_ || forced_ ? ProcessProgress::Unconfirmed : ProcessProgress::Pending;
  const DWORD workerWait = WaitForSingleObject(worker_.native_handle(), 0);
  if (workerWait == WAIT_FAILED) return ProcessProgress::Unconfirmed;
  if (workerWait != WAIT_OBJECT_0)
    return api_.now() >= launchDeadline_ || forced_ ? ProcessProgress::Unconfirmed : ProcessProgress::Pending;
  worker_.join();
  if (!workerCreated_) { settled_ = true; return ProcessProgress::Failed; }
  process_ = workerResult_.hProcess;
  closeChildren();
  BOOL inJob = FALSE;
  assigned_ = api_.inJob(process_, job_, &inJob) && inJob;
  const bool resumed = !forced_ && assigned_ && api_.now() < launchDeadline_ &&
      api_.resume(workerResult_.hThread) != DWORD(-1);
  api_.close(workerResult_.hThread);
  if (!resumed || api_.now() >= launchDeadline_) {
    forced_ = true;
    return forceCleanup(launchDeadline_) == ProcessProgress::Complete
        ? ProcessProgress::Failed : ProcessProgress::Unconfirmed;
  }
  established_ = true;
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
  if (worker_.joinable()) {
    pollLaunch();
    if (worker_.joinable()) return ProcessProgress::Unconfirmed;
    if (!process_ && settled_) return ProcessProgress::Complete;
  }
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
