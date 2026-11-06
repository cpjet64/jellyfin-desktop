#include "WindowsRestartProcess.h"
#include "WindowsRestartChannel.h"
#include "healthy_restart/module_identity_win.h"
#include "healthy_restart/module_gate.h"
#ifdef _WIN32
#include <algorithm>
#include <utility>

namespace FixtureRestart {
WindowsRestartProcess* WindowsRestartProcess::createHealthyWithChannels(const WindowsProcessApi& api,
    const std::vector<WindowsRestartChannel*>& channels, std::vector<WindowsRestartIo*> io,
    WindowsRestartChannel* identityChannel)
{
  if (!identityChannel || channels.empty() || channels.size() > 4 ||
      std::find(channels.begin(), channels.end(), identityChannel) == channels.end() ||
      !api.duplicate || !api.currentProcess || !api.currentProcessId || !api.processId || !api.handleInfo || !api.close) return nullptr;
  HANDLE parent = nullptr;
  const HANDLE current = api.currentProcess();
  if (!api.duplicate(current, current, current, &parent, PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, TRUE, 0)) return nullptr;
  DWORD flags = 0;
  if (!parent || parent == INVALID_HANDLE_VALUE || !api.handleInfo(parent, &flags) || !(flags & HANDLE_FLAG_INHERIT) ||
      api.processId(parent) != api.currentProcessId()) { if (parent && parent != INVALID_HANDLE_VALUE) api.close(parent); return nullptr; }
  auto* process = createWithChannels(api, channels, std::move(io));
  if (!process) { api.close(parent); return nullptr; }
  if (std::find(process->handles_.begin(), process->handles_.end(), parent) != process->handles_.end()) {
    api.close(parent); process->release(); return nullptr;
  }
  try { process->handles_.push_back(parent); }
  catch (...) { api.close(parent); process->release(); return nullptr; }
  process->healthyParentCredential_ = parent;
  process->healthyIdentityChannel_ = identityChannel;
  process->healthyRequired_ = true;
  return process;
}

bool WindowsRestartProcess::bindHealthyModuleLease(std::shared_ptr<HealthyModuleIdentity::StageLease>& lease,
    WindowsRestartChannel* channel, std::uint64_t launchId, const HealthyModuleIdentity::LaunchBinding& binding)
{
  if (!healthyRequired_ || launched_ || healthyLease_ || !lease || !launchId || channel != healthyIdentityChannel_ || !ownsChannel(channel)) return false;
  const auto found = std::find(channels_.begin(), channels_.end(), channel);
  const size_t index = static_cast<size_t>(found - channels_.begin());
  if (index >= io_.size() || !ownsEndpoint(channel, io_[index])) return false;
  const auto& observed = lease->observation();
  if (!observed.pathComplete || !observed.identityComplete || !observed.hashComplete || !observed.size ||
      observed.digest != HealthyModuleIdentity::expectedDigest()) return false;
  std::vector<std::uint8_t> frame;
  if (!HealthyModuleIdentity::frameIdentity(observed, launchId, binding, frame)) return false;
  if (!io_[index]->sealIdentityWriter()) return false;
  healthyLaunchId_ = launchId;
  healthyBinding_ = binding;
  healthyLease_ = std::move(lease);
  return true;
}

std::uintptr_t WindowsRestartProcess::healthyParentCredential() const
{
  return healthyRequired_ && !launched_ ? reinterpret_cast<std::uintptr_t>(healthyParentCredential_) : 0;
}

bool WindowsRestartProcess::healthyModuleEvidence(const WindowsRestartChannel* channel,
    HealthyModuleIdentity::FileObservation& output, std::uint64_t& launchId) const
{
  if (!healthyRequired_ || !healthyLease_ || channel != healthyIdentityChannel_) return false;
  const auto found = std::find(channels_.begin(), channels_.end(), channel);
  if (found == channels_.end()) return false;
  const size_t index = static_cast<size_t>(found - channels_.begin());
  if (index >= io_.size() || !ownsEndpoint(channel, io_[index])) return false;
  try {
    auto copy = healthyLease_->observation();
    output = std::move(copy);
  } catch (...) { return false; }
  launchId = healthyLaunchId_;
  return launchId != 0;
}

bool WindowsRestartProcess::healthyModuleFrame(const WindowsRestartChannel* channel,
    std::vector<std::uint8_t>& output) const
{
  HealthyModuleIdentity::FileObservation observation;
  std::uint64_t launchId = 0;
  return healthyModuleEvidence(channel, observation, launchId) &&
         HealthyModuleIdentity::frameIdentity(observation, launchId, healthyBinding_, output);
}

ProcessProgress WindowsRestartProcess::sendHealthyModuleFrame(ULONGLONG deadline)
{
  if (!healthyRequired_ || !launchEstablished() || deadline != launchDeadline_ || healthyFrameState_ > 1 || api_.now() >= deadline ||
      (healthyFrameState_ && deadline != healthyFrameDeadline_))
    return ProcessProgress::Failed;
  const auto found = std::find(channels_.begin(), channels_.end(), healthyIdentityChannel_);
  if (found == channels_.end()) return ProcessProgress::Failed;
  const size_t index = static_cast<size_t>(found - channels_.begin());
  if (index >= io_.size() || !ownsEndpoint(healthyIdentityChannel_, io_[index])) return ProcessProgress::Failed;
  auto* io = io_[index];
  IoProgress result;
  if (!healthyFrameState_) {
    std::vector<std::uint8_t> frame;
    if (!healthyModuleFrame(healthyIdentityChannel_, frame) || frame.empty()) return ProcessProgress::Failed;
    healthyFrameState_ = 1;
    healthyFrameDeadline_ = deadline;
    result = io->beginIdentity(healthyIdentityChannel_->parentHandle(), frame.data(), DWORD(frame.size()), deadline);
  } else {
    result = io->pollIdentity();
  }
  if (result == IoProgress::Pending) return ProcessProgress::Pending;
  if (result == IoProgress::Unconfirmed) return ProcessProgress::Unconfirmed;
  healthyFrameState_ = 2;
  if (result != IoProgress::Complete || !closeParentChannel(healthyIdentityChannel_)) return ProcessProgress::Failed;
  return ProcessProgress::Complete;
}

ProcessProgress WindowsRestartProcess::cancelHealthyModuleFrame()
{
  if (!healthyRequired_ || healthyFrameState_ != 1) return ProcessProgress::Failed;
  const auto found = std::find(channels_.begin(), channels_.end(), healthyIdentityChannel_);
  if (found == channels_.end()) return ProcessProgress::Failed;
  const size_t index = static_cast<size_t>(found - channels_.begin());
  if (index >= io_.size() || !ownsEndpoint(healthyIdentityChannel_, io_[index])) return ProcessProgress::Failed;
  const auto result = io_[index]->cancelImpl();
  if (result == IoProgress::Unconfirmed) return ProcessProgress::Unconfirmed;
  if (result == IoProgress::Pending) return ProcessProgress::Pending;
  healthyFrameState_ = 2;
  return ProcessProgress::Failed;
}
}
#endif
