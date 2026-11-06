#include "module_endpoints.h"
#include <utility>
#include <functional>
#ifdef _WIN32
#include <windows.h>
#endif

namespace HealthyModuleIdentity {
#ifdef _WIN32
enum class Comparison { Same, Different, Error };
struct ChildEndpoints::Operations {
  std::function<bool()> valid;
  std::function<bool(std::uintptr_t)> accepts;
  std::function<Comparison(std::uintptr_t, std::uintptr_t)> compare;
  std::function<bool(std::uintptr_t, HANDLE&)> duplicate;
  std::function<bool(HANDLE)> close;
  FixtureRestart::WindowsIoApi io;
};
#endif
struct ChildEndpoints::State {
  struct Entry {
    EndpointRole role = EndpointRole::Control;
#ifdef _WIN32
    HANDLE pipe = nullptr;
    FixtureRestart::WindowsRestartIo* io = nullptr;
    bool reading = false, readComplete = false, writeComplete = false, closeFailed = false;
    bool trackedGrant = false, containment = false;
#endif
  } entries[3];
  std::shared_ptr<const VerifiedModule> module;
  std::uint64_t generation = 0, deadline = 0;
  EndpointMode mode = EndpointMode::Source;
#ifdef _WIN32
  Operations operations;
#endif
  ~State() {
#ifdef _WIN32
    for (auto& entry : entries) {
      if (entry.io) entry.io->release();
      if (entry.pipe) operations.close(entry.pipe);
    }
#endif
  }
  Entry* find(EndpointRole role) noexcept {
    for (auto& entry : entries) if (entry.role == role) return &entry;
    return nullptr;
  }
#ifdef _WIN32
  bool authorityValid() {
    try { if (operations.valid && operations.valid()) return true; } catch (...) {}
    for (auto& entry : entries) entry.containment = true;
    return false;
  }
#endif
};
ChildEndpoints::ChildEndpoints(std::unique_ptr<State> state) : state_(std::move(state)) {}
ChildEndpoints::~ChildEndpoints()
{
#ifdef _WIN32
  if (state_) for (const auto& entry : state_->entries)
    if (entry.closeFailed || (entry.io && !entry.io->canReleaseStorage())) { (void)state_.release(); break; }
#endif
}
std::unique_ptr<ChildEndpoints> ChildEndpoints::create(std::unique_ptr<VerifiedModule>& module,
    std::uint64_t generation, std::uint64_t deadline, EndpointMode mode, const EndpointInput (&inputs)[3])
{
#ifdef _WIN32
  if (!module) return {};
  Operations ops;
  auto* capability = module.get();
  ops.valid = [capability, generation] { return capability->stillValid(generation); };
  ops.accepts = [capability, generation, deadline](std::uintptr_t pipe) { return capability->acceptsEndpoint(pipe, generation, deadline); };
  ops.compare = [](std::uintptr_t a, std::uintptr_t b) {
    SetLastError(ERROR_SUCCESS);
    if (CompareObjectHandles(reinterpret_cast<HANDLE>(a), reinterpret_cast<HANDLE>(b))) return Comparison::Same;
    return GetLastError() == ERROR_NOT_SAME_OBJECT ? Comparison::Different : Comparison::Error;
  };
  ops.duplicate = [](std::uintptr_t source, HANDLE& out) { return DuplicateHandle(GetCurrentProcess(), reinterpret_cast<HANDLE>(source), GetCurrentProcess(), &out, 0, FALSE, DUPLICATE_SAME_ACCESS) != FALSE; };
  ops.close = [](HANDLE handle) { return CloseHandle(handle) != FALSE; };
  return createImpl(module, generation, deadline, mode, inputs, std::move(ops));
#else
  (void)module; (void)generation; (void)deadline; (void)mode; (void)inputs;
  return {};
#endif
}
#ifdef _WIN32
std::unique_ptr<ChildEndpoints> ChildEndpoints::createImpl(std::unique_ptr<VerifiedModule>& module,
    std::uint64_t generation, std::uint64_t deadline, EndpointMode mode, const EndpointInput (&inputs)[3], Operations ops)
{
  try {
    if (!ops.valid || !ops.accepts || !ops.compare || !ops.duplicate || !ops.close || !ops.valid()) return {};
    const auto required = mode == EndpointMode::Source ? EndpointRole::Evidence : EndpointRole::Grant;
    if (mode != EndpointMode::Source && mode != EndpointMode::Restore) return {};
    for (int i = 0; i < 3; ++i) {
      if (!inputs[i].inheritedPipe || (inputs[i].role != required && inputs[i].role != EndpointRole::Control &&
          inputs[i].role != EndpointRole::Ack) || !ops.accepts(inputs[i].inheritedPipe)) return {};
      for (int j = 0; j < i; ++j)
        if (inputs[i].role == inputs[j].role || ops.compare(inputs[i].inheritedPipe, inputs[j].inheritedPipe) != Comparison::Different) return {};
    }
    auto state = std::make_unique<State>();
    state->generation = generation; state->deadline = deadline; state->mode = mode;
    state->operations = std::move(ops);
    for (int i = 0; i < 3; ++i) {
      auto& entry = state->entries[i];
      entry.role = inputs[i].role;
      if (!state->operations.duplicate(inputs[i].inheritedPipe, entry.pipe)) return {};
      if (!state->operations.accepts(reinterpret_cast<std::uintptr_t>(entry.pipe))) return {};
      for (int j = 0; j < i; ++j)
        if (state->operations.compare(reinterpret_cast<std::uintptr_t>(entry.pipe),
            reinterpret_cast<std::uintptr_t>(state->entries[j].pipe)) != Comparison::Different) return {};
      entry.io = new FixtureRestart::WindowsRestartIo(state->operations.io);
      if (!entry.io->bind(entry.pipe)) return {};
    }
    if (!state->operations.valid()) return {};
    auto result = std::unique_ptr<ChildEndpoints>(new ChildEndpoints(std::move(state)));
    result->state_->module = std::move(module);
    return result;
  } catch (...) { return {}; }
}
#if defined(HEALTHY_ENDPOINT_TESTING)
std::unique_ptr<ChildEndpoints> ChildEndpoints::createForTest(std::uint64_t deadline, EndpointMode mode,
    const EndpointInput (&inputs)[3], const TestOperations& test)
{
  std::unique_ptr<VerifiedModule> none;
  return createForTest(none, 1, deadline, mode, inputs, test);
}
std::unique_ptr<ChildEndpoints> ChildEndpoints::createForTest(std::unique_ptr<VerifiedModule>& module,
    std::uint64_t generation, std::uint64_t deadline, EndpointMode mode,
    const EndpointInput (&inputs)[3], const TestOperations& test)
{
  Operations ops;
  ops.valid = test.valid; ops.accepts = test.accepts;
  ops.compare = [test](std::uintptr_t a, std::uintptr_t b) {
    const auto value = test.compare(a, b);
    return value == EndpointComparison::Same ? Comparison::Same :
        value == EndpointComparison::Different ? Comparison::Different : Comparison::Error;
  };
  if (!test.compare) return {};
  ops.duplicate = test.duplicate; ops.close = test.close; ops.io = test.io;
  return createImpl(module, generation, deadline, mode, inputs, std::move(ops));
}
#endif
#endif
const void* ChildEndpoints::token(EndpointRole role) const noexcept
{
#ifdef _WIN32
  auto* entry = state_ && state_->authorityValid() ? state_->find(role) : nullptr;
  return entry && entry->pipe && !entry->closeFailed && !entry->containment ? entry : nullptr;
#else
  (void)role;
  return nullptr;
#endif
}
std::shared_ptr<const VerifiedModule> ChildEndpoints::capability() const noexcept
{
  return state_ ? state_->module : nullptr;
}
#ifdef _WIN32
void ChildEndpoints::beginContainment() noexcept
{
  if (!state_) return;
  for (auto& entry : state_->entries) entry.containment = true;
}
FixtureRestart::IoProgress ChildEndpoints::begin(EndpointRole role, bool read, const void* data, DWORD size)
{
  auto* entry = state_ ? state_->find(role) : nullptr;
  if (!entry || !entry->pipe || entry->closeFailed || entry->trackedGrant || entry->containment ||
      !entry->io->canReleaseStorage() || !state_->authorityValid()) return FixtureRestart::IoProgress::Failed;
  const bool expectedRead = role == EndpointRole::Control || (state_->mode == EndpointMode::Restore && role == EndpointRole::Grant);
  if (read != expectedRead) return FixtureRestart::IoProgress::Failed;
  entry->reading = read; entry->readComplete = false; entry->writeComplete = false;
  const auto authority = [state = state_.get(), entry] { return state->authorityValid() && !entry->containment; };
  const auto result = entry->io->beginGuarded(entry->pipe, read, data, size, state_->deadline, authority);
  if (entry->containment) return result == FixtureRestart::IoProgress::Pending ? result : FixtureRestart::IoProgress::Failed;
  entry->readComplete = read && result == FixtureRestart::IoProgress::Complete;
  entry->writeComplete = !read && result == FixtureRestart::IoProgress::Complete;
  return result;
}
FixtureRestart::IoProgress ChildEndpoints::beginTrackedGrantRead(DWORD size)
{
  auto* entry = state_ ? state_->find(EndpointRole::Grant) : nullptr;
  if (!entry || state_->mode != EndpointMode::Restore || !entry->pipe || entry->closeFailed ||
      entry->trackedGrant || entry->containment || !entry->io->canReleaseStorage() || !state_->authorityValid())
    return FixtureRestart::IoProgress::Failed;
  const auto previous = entry->io->operation();
  entry->trackedGrant = true;
  entry->reading = true; entry->readComplete = false; entry->writeComplete = false;
  const auto result = entry->io->beginTrackedRead(entry->pipe, size, state_->deadline);
  if (entry->io->operation() == previous && !entry->io->pending() && !entry->io->settlement())
    entry->trackedGrant = false;
  return result;
}
std::uint64_t ChildEndpoints::grantReadOperation() const noexcept
{
  auto* entry = state_ ? state_->find(EndpointRole::Grant) : nullptr;
  return entry && entry->trackedGrant ? entry->io->operation() : 0;
}
std::shared_ptr<const FixtureRestart::WindowsRestartIo::Settlement>
ChildEndpoints::grantReadSettlement() const noexcept
{
  auto* entry = state_ ? state_->find(EndpointRole::Grant) : nullptr;
  return entry && entry->trackedGrant ? entry->io->settlement() : nullptr;
}
bool ChildEndpoints::consumeGrantReadSettlement(std::uint64_t operation)
{
  auto* entry = state_ ? state_->find(EndpointRole::Grant) : nullptr;
  if (!entry || !entry->trackedGrant || !entry->io->consumeSettlement(operation)) return false;
  entry->trackedGrant = false;
  return true;
}
FixtureRestart::IoProgress ChildEndpoints::poll(EndpointRole role)
{
  auto* entry = state_ ? state_->find(role) : nullptr;
  if (!entry || !entry->pipe || entry->closeFailed) return FixtureRestart::IoProgress::Failed;
  (void)state_->authorityValid();
  if (entry->containment) {
    entry->readComplete = entry->writeComplete = false;
    return entry->io->pollSettlementOnly();
  }
  const bool tracked = entry->trackedGrant;
  const auto authority = [state = state_.get(), entry] { return state->authorityValid() && !entry->containment; };
  const auto result = tracked ? entry->io->poll() : entry->io->pollGuarded(authority);
  if (entry->containment) return result == FixtureRestart::IoProgress::Pending || result == FixtureRestart::IoProgress::Unconfirmed
      ? result : FixtureRestart::IoProgress::Failed;
  entry->readComplete = !tracked && entry->reading && result == FixtureRestart::IoProgress::Complete;
  entry->writeComplete = !tracked && !entry->reading && result == FixtureRestart::IoProgress::Complete;
  return result;
}
FixtureRestart::IoProgress ChildEndpoints::cancel(EndpointRole role)
{
  auto* entry = state_ ? state_->find(role) : nullptr;
  if (!entry || !entry->pipe || entry->closeFailed) return FixtureRestart::IoProgress::Failed;
  entry->readComplete = false;
  entry->reading = false;
  entry->writeComplete = false;
  (void)state_->authorityValid();
  if (entry->containment) return entry->io->cancelSettlementOnly();
  const auto authority = [state = state_.get(), entry] { return state->authorityValid() && !entry->containment; };
  const auto result = entry->io->cancelGuarded(authority);
  if (entry->containment && result != FixtureRestart::IoProgress::Pending && result != FixtureRestart::IoProgress::Unconfirmed)
    return FixtureRestart::IoProgress::Failed;
  return result;
}
std::shared_ptr<const FixtureRestart::WindowsRestartIo::Settlement>
ChildEndpoints::retainedSettlement(EndpointRole role) const noexcept
{
  auto* entry = state_ ? state_->find(role) : nullptr;
  return entry && entry->containment ? entry->io->settlement() : nullptr;
}
bool ChildEndpoints::consumeRetainedSettlement(EndpointRole role, std::uint64_t operation)
{
  auto* entry = state_ ? state_->find(role) : nullptr;
  if (!entry || !entry->containment || !entry->io->consumeSettlement(operation)) return false;
  entry->trackedGrant = false;
  return true;
}
bool ChildEndpoints::closeSettled(EndpointRole role)
{
  auto* entry = state_ ? state_->find(role) : nullptr;
  if (!entry || !entry->pipe || entry->closeFailed || entry->containment ||
      (role != EndpointRole::Evidence && role != EndpointRole::Ack) ||
      !entry->io || !entry->writeComplete || !entry->io->canReleaseStorage() ||
      !state_->authorityValid()) return false;
  entry->closeFailed = true; // Reentrant use cannot issue another close.
  try { if (!state_->operations.close(entry->pipe)) return false; }
  catch (...) { return false; }
  entry->pipe = nullptr;
  entry->closeFailed = false;
  entry->writeComplete = false;
  return true;
}
bool ChildEndpoints::canceledWithoutData(EndpointRole role) const
{
  auto* entry = state_ ? state_->find(role) : nullptr;
  return entry && entry->pipe && !entry->closeFailed && !entry->containment && entry->io &&
      state_->authorityValid() && entry->io->canceledWithoutData();
}
const std::vector<unsigned char>* ChildEndpoints::bytes(EndpointRole role) const noexcept
{
  auto* entry = state_ ? state_->find(role) : nullptr;
  return entry && !entry->containment && entry->readComplete && state_->authorityValid() && !entry->io->pending() ? &entry->io->bytes() : nullptr;
}
#endif
}
