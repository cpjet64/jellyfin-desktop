#include "module_channel.h"
#include "module_gate.h"
#include "module_import_prepare.h"
#ifdef _WIN32
#include "../fixture_restart/WindowsFrameReader.h"
#include <windows.h>
#endif
#include <utility>

namespace HealthyModuleIdentity {
struct VerifiedModule::State {
#ifdef _WIN32
  HANDLE parent = nullptr;
  HANDLE identityPipe = nullptr;
  ~State() { if (identityPipe) CloseHandle(identityPipe); if (parent) CloseHandle(parent); }
#endif
  std::uint64_t generation = 0, deadline = 0;
  LaunchBinding binding;
  std::unique_ptr<ImportedModule> module;
};
VerifiedModule::VerifiedModule(std::unique_ptr<State> state) : state_(std::move(state)) {}
VerifiedModule::~VerifiedModule() = default;
bool VerifiedModule::stillValid(std::uint64_t generation) const noexcept
{
#if defined(_WIN32) && defined(_M_X64)
  return state_ && generation && generation == state_->generation && state_->parent &&
      GetTickCount64() < state_->deadline && WaitForSingleObject(state_->parent, 0) == WAIT_TIMEOUT &&
      state_->module && state_->module->stillBound();
#else
  (void)generation;
  return false;
#endif
}
SourceContract VerifiedModule::renderSourceContract(std::uint64_t generation) const noexcept
{ return stillValid(generation) ? SourceContract::BuilderSourceQualified : SourceContract::Unqualified; }
bool VerifiedModule::launchBinding(std::uint64_t generation, LaunchBinding& output) const noexcept
{
  if (!stillValid(generation)) return false;
  output = state_->binding;
  return true;
}
SourceContract VerifiedModule::wakeSourceContract(std::uint64_t generation) const noexcept
{ return stillValid(generation) ? SourceContract::BuilderSourceQualified : SourceContract::Unqualified; }
bool VerifiedModule::acceptsEndpoint(std::uintptr_t pipe, std::uint64_t generation, std::uint64_t deadline) const noexcept
{
#ifdef _WIN32
  if (!stillValid(generation) || deadline != state_->deadline || !pipe) return false;
  HANDLE handle = reinterpret_cast<HANDLE>(pipe);
  ULONG peer = 0;
  if (!state_->identityPipe) return false;
  SetLastError(ERROR_SUCCESS);
  if (CompareObjectHandles(handle, state_->identityPipe) || GetLastError() != ERROR_NOT_SAME_OBJECT) return false;
  return GetFileType(handle) == FILE_TYPE_PIPE && GetNamedPipeServerProcessId(handle, &peer) &&
      peer && peer == GetProcessId(state_->parent);
#else
  (void)pipe; (void)generation; (void)deadline;
  return false;
#endif
}

struct ChildModuleGate::State {
#ifdef _WIN32
  HANDLE pipe = nullptr, parent = nullptr;
  FixtureRestart::WindowsRestartIo* io = nullptr;
  std::unique_ptr<FixtureRestart::WindowsFrameReader> reader;
  ~State() {
    reader.reset();
    if (io) io->release();
    if (pipe) CloseHandle(pipe);
    if (parent) CloseHandle(parent);
  }
#endif
  std::uint64_t launch = 0, generation = 0, deadline = 0;
  std::uint64_t expectedRun = 0;
  LaunchRole expectedRole = LaunchRole::Source;
  ModuleGateProgress progress = ModuleGateProgress::Failed;
  bool begun = false;
  std::unique_ptr<VerifiedModule> verified;
};
ChildModuleGate::ChildModuleGate(std::unique_ptr<State> state) : state_(std::move(state)) {}
ChildModuleGate::~ChildModuleGate()
{
#ifdef _WIN32
  // Preserve OVERLAPPED storage and handles if cancellation did not settle.
  if (state_ && state_->io && state_->io->pending()) (void)state_.release();
#endif
}

std::unique_ptr<ChildModuleGate> ChildModuleGate::create(std::uintptr_t pipe,
    std::uintptr_t parentProcess, std::uint64_t launch, std::uint64_t generation,
    std::uint64_t deadline, std::uint64_t expectedRun, LaunchRole expectedRole)
{
#ifdef _WIN32
  try {
    if (!pipe || !parentProcess || !launch || !generation || !expectedRun ||
        (expectedRole != LaunchRole::Source && expectedRole != LaunchRole::Restore) || GetTickCount64() >= deadline) return {};
    const HANDLE sourcePipe = reinterpret_cast<HANDLE>(pipe);
    const HANDLE sourceParent = reinterpret_cast<HANDLE>(parentProcess);
    ULONG peer = 0;
    if (GetFileType(sourcePipe) != FILE_TYPE_PIPE || !GetNamedPipeServerProcessId(sourcePipe, &peer) ||
        !peer || GetProcessId(sourceParent) != peer || WaitForSingleObject(sourceParent, 0) != WAIT_TIMEOUT) return {};
    auto s = std::make_unique<State>();
    HANDLE self = GetCurrentProcess();
    if (!DuplicateHandle(self, sourcePipe, self, &s->pipe, 0, FALSE, DUPLICATE_SAME_ACCESS) ||
        !DuplicateHandle(self, sourceParent, self, &s->parent, 0, FALSE, DUPLICATE_SAME_ACCESS)) return {};
    s->io = new FixtureRestart::WindowsRestartIo(FixtureRestart::WindowsIoApi{});
    if (!s->io->bind(s->pipe)) return {};
    s->reader = std::make_unique<FixtureRestart::WindowsFrameReader>(*s->io, s->pipe);
    s->launch = launch; s->generation = generation; s->deadline = deadline;
    s->expectedRun = expectedRun; s->expectedRole = expectedRole;
    return std::unique_ptr<ChildModuleGate>(new ChildModuleGate(std::move(s)));
  } catch (...) { return {}; }
#else
  (void)pipe; (void)parentProcess; (void)launch; (void)generation; (void)deadline; (void)expectedRun; (void)expectedRole;
  return {};
#endif
}

ModuleGateProgress ChildModuleGate::begin()
{
#ifdef _WIN32
  if (!state_ || state_->begun) return ModuleGateProgress::Failed;
  state_->begun = true;
  const auto r = state_->reader->begin(state_->deadline);
  if (r == FixtureRestart::IoProgress::Complete) return finish();
  state_->progress = r == FixtureRestart::IoProgress::Pending ? ModuleGateProgress::Pending :
      r == FixtureRestart::IoProgress::Unconfirmed ? ModuleGateProgress::Unconfirmed : ModuleGateProgress::Failed;
  return state_->progress;
#else
  return ModuleGateProgress::Failed;
#endif
}
ModuleGateProgress ChildModuleGate::poll()
{
#ifdef _WIN32
  if (!state_ || state_->progress != ModuleGateProgress::Pending) return ModuleGateProgress::Failed;
  const auto r = state_->reader->poll();
  if (r == FixtureRestart::IoProgress::Complete) return finish();
  state_->progress = r == FixtureRestart::IoProgress::Pending ? ModuleGateProgress::Pending :
      r == FixtureRestart::IoProgress::Unconfirmed ? ModuleGateProgress::Unconfirmed : ModuleGateProgress::Failed;
  return state_->progress;
#else
  return ModuleGateProgress::Failed;
#endif
}
ModuleGateProgress ChildModuleGate::cancel()
{
#ifdef _WIN32
  if (!state_ || state_->progress != ModuleGateProgress::Pending) return ModuleGateProgress::Failed;
  const auto r = state_->reader->cancel();
  state_->progress = r == FixtureRestart::IoProgress::Pending ? ModuleGateProgress::Pending :
      r == FixtureRestart::IoProgress::Unconfirmed ? ModuleGateProgress::Unconfirmed : ModuleGateProgress::Failed;
  return state_->progress;
#else
  return ModuleGateProgress::Failed;
#endif
}

ModuleGateProgress ChildModuleGate::finish()
{
#ifdef _WIN32
  try {
    state_->progress = ModuleGateProgress::Failed;
    if (GetTickCount64() >= state_->deadline || WaitForSingleObject(state_->parent, 0) != WAIT_TIMEOUT) return state_->progress;
    const auto* frame = state_->reader->completedFrame();
    if (!frame || frame->size() <= 4 || frame->size() > qsizetype(MaxIdentityFrame + 4)) return state_->progress;
    const auto* begin = reinterpret_cast<const std::uint8_t*>(frame->constData()) + 4;
    LaunchIdentity expected;
    if (!decodeIdentity(std::vector<std::uint8_t>(begin, begin + frame->size() - 4), expected) ||
        expected.launch != state_->launch || expected.binding.run != state_->expectedRun ||
        expected.binding.role != state_->expectedRole) return state_->progress;
    ImportPreparation preparation;
    if (!preparation.initialize()) return state_->progress;
    auto imported = preparation.takeObservation();
    if (!imported || !imported->stillBound()) return state_->progress;
    const auto& file = imported->observation();
    Evidence e;
    e.platformAvailable = e.parentLeaseHeld = e.channelBound = e.moduleReferenceHeld = true;
    e.modulePathComplete = file.pathComplete;
    e.parentHashComplete = e.parentIdentityComplete = true;
    e.childHashComplete = file.hashComplete; e.childIdentityComplete = file.identityComplete;
    e.expectedLaunch = state_->launch; e.observedLaunch = expected.launch;
    e.expectedSize = expected.size; e.observedSize = file.size;
    e.expectedFile = expected.file; e.observedFile = file.identity;
    e.expectedPath = expected.canonicalPath; e.observedPath = file.canonicalPath;
    e.parentDigest = expected.digest; e.childDigest = file.digest;
    e.callSiteSlot = imported->slot(); e.firstTarget = imported->firstTarget();
    e.secondTarget = imported->secondTarget(); e.targetModule = imported->module();
    e.executableModule = imported->executable();
    if (compare(e) != Rejection::None || GetTickCount64() >= state_->deadline ||
        WaitForSingleObject(state_->parent, 0) != WAIT_TIMEOUT) return state_->progress;
    auto verified = std::make_unique<VerifiedModule::State>();
    verified->generation = state_->generation; verified->deadline = state_->deadline;
    verified->binding = expected.binding;
    verified->parent = state_->parent; state_->parent = nullptr;
    verified->identityPipe = state_->pipe; state_->pipe = nullptr;
    verified->module = std::move(imported);
    state_->verified.reset(new VerifiedModule(std::move(verified)));
    state_->progress = ModuleGateProgress::Ready;
  } catch (...) { state_->progress = ModuleGateProgress::Failed; }
  return state_->progress;
#else
  return ModuleGateProgress::Failed;
#endif
}
std::unique_ptr<VerifiedModule> ChildModuleGate::take()
{
  if (!state_ || state_->progress != ModuleGateProgress::Ready) return {};
  state_->progress = ModuleGateProgress::Failed;
  if (!state_->verified || !state_->verified->stillValid(state_->generation)) {
    state_->verified.reset();
    return {};
  }
  return std::move(state_->verified);
}
}
