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
  std::function<void(HANDLE)> close;
  FixtureRestart::WindowsIoApi io;
};
#endif
struct ChildEndpoints::State {
  struct Entry {
    EndpointRole role = EndpointRole::Control;
#ifdef _WIN32
    HANDLE pipe = nullptr;
    FixtureRestart::WindowsRestartIo* io = nullptr;
    bool reading = false, readComplete = false;
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
};
ChildEndpoints::ChildEndpoints(std::unique_ptr<State> state) : state_(std::move(state)) {}
ChildEndpoints::~ChildEndpoints()
{
#ifdef _WIN32
  if (state_) for (const auto& entry : state_->entries)
    if (entry.io && entry.io->pending()) { (void)state_.release(); break; }
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
  ops.close = [](HANDLE handle) { CloseHandle(handle); };
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
  Operations ops;
  ops.valid = test.valid; ops.accepts = test.accepts;
  ops.compare = [test](std::uintptr_t a, std::uintptr_t b) {
    const auto value = test.compare(a, b);
    return value == EndpointComparison::Same ? Comparison::Same :
        value == EndpointComparison::Different ? Comparison::Different : Comparison::Error;
  };
  if (!test.compare) return {};
  ops.duplicate = test.duplicate; ops.close = test.close; ops.io = test.io;
  std::unique_ptr<VerifiedModule> none;
  return createImpl(none, 1, deadline, mode, inputs, std::move(ops));
}
#endif
#endif
const void* ChildEndpoints::token(EndpointRole role) const noexcept
{
#ifdef _WIN32
  return state_ && state_->operations.valid() ? state_->find(role) : nullptr;
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
FixtureRestart::IoProgress ChildEndpoints::begin(EndpointRole role, bool read, const void* data, DWORD size)
{
  auto* entry = state_ ? state_->find(role) : nullptr;
  if (!entry || !state_->operations.valid()) return FixtureRestart::IoProgress::Failed;
  const bool expectedRead = role == EndpointRole::Control || (state_->mode == EndpointMode::Restore && role == EndpointRole::Grant);
  if (read != expectedRead) return FixtureRestart::IoProgress::Failed;
  entry->reading = read; entry->readComplete = false;
  const auto result = entry->io->begin(entry->pipe, read, data, size, state_->deadline);
  entry->readComplete = read && result == FixtureRestart::IoProgress::Complete;
  return result;
}
FixtureRestart::IoProgress ChildEndpoints::poll(EndpointRole role)
{
  auto* entry = state_ ? state_->find(role) : nullptr;
  if (!entry || !state_->operations.valid()) return FixtureRestart::IoProgress::Failed;
  const auto result = entry->io->poll();
  entry->readComplete = entry->reading && result == FixtureRestart::IoProgress::Complete;
  return result;
}
FixtureRestart::IoProgress ChildEndpoints::cancel(EndpointRole role)
{
  auto* entry = state_ ? state_->find(role) : nullptr;
  if (!entry) return FixtureRestart::IoProgress::Failed;
  entry->readComplete = false;
  entry->reading = false;
  return entry->io->cancel();
}
const std::vector<unsigned char>* ChildEndpoints::bytes(EndpointRole role) const noexcept
{
  auto* entry = state_ ? state_->find(role) : nullptr;
  return entry && entry->readComplete && state_->operations.valid() && !entry->io->pending() ? &entry->io->bytes() : nullptr;
}
#endif
}
