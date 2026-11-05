#pragma once

#include <cstdint>
#include <initializer_list>
#include <utility>

// Pure coordinator: one owner thread retains ownership and supplies operations.
// It rejects same-thread reentry; it is not a cross-thread synchronization tool.
// runScenario catches scenario exceptions while the external owner remains alive.
namespace HealthyCleanup {
enum class Context { Never, Present, Unknown };
enum class StopStatus { NotObserved, GuardRejected, Unavailable, Error, Accepted };
enum class Step {
  None, CleanupStop, Terminal, ValidateNoContext, ValidateIdentity, CloseRenderAdmission,
  ValidateRenderContext, UnregisterRender, DrainRenderCallbacks, RejectQueuedRender,
  FreeRender, ClearRenderPointer, AwaitRenderAck, CloseWakeAdmission, UnregisterWake,
  DrainWakeCallbacks, InvalidatePlayerEvents, InvalidateHooks, FenceWorker,
  CloseSecondaryObserver, DestroyOwned
};
struct Identity { std::uint64_t generation = 0, thread = 0, context = 0; };
struct Stop { StopStatus status = StopStatus::NotObserved; int error = 0; };
struct Input {
  bool originalVerdict = false;
  bool scenarioException = false;
  Stop scenarioStop;
  bool cleanupStopRequired = false;
  Context context = Context::Unknown;
  Identity identity;
  std::int64_t deadline = 0;
};
// freeReturned records a void return, not a native success code.
// pointerCleared/acknowledged mean verified before deadline; false does not
// prove the operation had no effect. retained means the owner survives, not
// that its resource graph remains intact after an attempted free.
struct Report {
  bool originalVerdict = false, entered = false, repeatRejected = false;
  bool cleanupStopAttempted = false, freeAttempted = false, freeReturned = false;
  bool pointerCleared = false, acknowledged = false, destructionAttempted = false;
  bool completed = false, retained = true, ownershipUncertain = false, scenarioException = false;
  Stop scenarioStop, cleanupStop;
  Step failed = Step::None;
};

// All methods must be nonthrowing, bounded by the supplied absolute monotonic deadline,
// and may not destroy the owner except in DestroyOwned. An adapter must run
// render operations in order on the original render thread with that context
// current, retain callback tokens through unregister and drain, never hold an
// admission mutex across unregister, and reject stale queued work by generation.
// InvalidatePlayerEvents, InvalidateHooks and FenceWorker are per owner and
// generation; they must not drain global or unrelated Qt event queues.
// ValidateNoContext proves no context or callback was ever installed. Terminal
// proves actual idle/no-VO and force-window state, or proves no core/playback
// existed when setup failed before binding. AwaitRenderAck succeeds only
// for the supplied exact identity after free returned and the pointer was cleared.
// DestroyOwned must be preflighted and bounded: a failed or late return means
// partial destruction is possible and is reported as ownership uncertain.
class Operations {
public:
  virtual ~Operations() = default;
  virtual std::int64_t now() noexcept = 0;
  virtual Stop cleanupStop(std::int64_t deadline) noexcept = 0;
  virtual bool perform(Step step, const Identity& identity, std::int64_t deadline) noexcept = 0;
  // The native free API returns void. A return is observable, not a success code.
  virtual void freeRender(const Identity& identity, std::int64_t deadline) noexcept = 0;
};

class Coordinator {
public:
  Coordinator() = default;
  Coordinator(const Coordinator&) = delete;
  Coordinator& operator=(const Coordinator&) = delete;
  bool entered() const noexcept { return entered_; }
  bool beginScenario() noexcept
  {
    if (entered_ || scenarioActive_) { report_.repeatRejected = true; return false; }
    scenarioActive_ = true;
    return true;
  }
  void endScenario() noexcept { scenarioActive_ = false; }
  Report currentReport() const noexcept { return report_; }
  Report run(const Input input, Operations& ops) noexcept
  {
    if (entered_ || scenarioActive_) { report_.repeatRejected = true; return report_; }
    entered_ = true;
    report_.entered = true;
    report_.originalVerdict = input.originalVerdict;
    report_.scenarioException = input.scenarioException;
    report_.scenarioStop = input.scenarioStop;
    if (!terminal(input, ops) || !render(input, ops) || !producers(input, ops)) return report_;
    if (!ready(input, ops, Step::DestroyOwned)) return report_;
    report_.destructionAttempted = true;
    const bool destroyed = ops.perform(Step::DestroyOwned, input.identity, input.deadline);
    if (!destroyed || ops.now() >= input.deadline) {
      report_.failed = Step::DestroyOwned;
      report_.retained = false;
      report_.ownershipUncertain = true;
      return report_;
    }
    report_.completed = true;
    report_.retained = false;
    return report_;
  }

private:
  bool ready(const Input& input, Operations& ops, Step step) noexcept
  {
    if (ops.now() < input.deadline) return true;
    report_.failed = step;
    return false;
  }
  bool call(const Input& input, Operations& ops, Step step) noexcept
  {
    if (!ready(input, ops, step)) return false;
    const bool success = ops.perform(step, input.identity, input.deadline);
    if (!success || ops.now() >= input.deadline) { report_.failed = step; return false; }
    return true;
  }
  bool terminal(const Input& input, Operations& ops) noexcept
  {
    if (input.cleanupStopRequired) {
      if (!ready(input, ops, Step::CleanupStop)) return false;
      report_.cleanupStopAttempted = true;
      report_.cleanupStop = ops.cleanupStop(input.deadline);
      if (report_.cleanupStop.status != StopStatus::Accepted || ops.now() >= input.deadline) {
        report_.failed = Step::CleanupStop;
        return false;
      }
    }
    return call(input, ops, Step::Terminal);
  }
  bool render(const Input& input, Operations& ops) noexcept
  {
    if (input.context == Context::Unknown) { report_.failed = Step::ValidateNoContext; return false; }
    if (input.context == Context::Never) return call(input, ops, Step::ValidateNoContext);
    if (!input.identity.generation || !input.identity.thread || !input.identity.context) {
      report_.failed = Step::ValidateIdentity;
      return false;
    }
    if (!call(input, ops, Step::CloseRenderAdmission) || !call(input, ops, Step::ValidateRenderContext) ||
        !call(input, ops, Step::UnregisterRender) || !call(input, ops, Step::DrainRenderCallbacks) ||
        !call(input, ops, Step::RejectQueuedRender)) return false;
    if (!ready(input, ops, Step::FreeRender)) return false;
    report_.freeAttempted = true;
    ops.freeRender(input.identity, input.deadline);
    report_.freeReturned = true;
    if (ops.now() >= input.deadline) { report_.failed = Step::FreeRender; return false; }
    if (!call(input, ops, Step::ClearRenderPointer)) return false;
    report_.pointerCleared = true;
    if (!call(input, ops, Step::AwaitRenderAck)) return false;
    report_.acknowledged = true;
    return true;
  }
  bool producers(const Input& input, Operations& ops) noexcept
  {
    for (auto step : {Step::CloseWakeAdmission, Step::UnregisterWake, Step::DrainWakeCallbacks,
                      Step::InvalidatePlayerEvents, Step::InvalidateHooks, Step::FenceWorker,
                      Step::CloseSecondaryObserver}) {
      if (!call(input, ops, step)) return false;
    }
    return true;
  }
  bool entered_ = false;
  bool scenarioActive_ = false;
  Report report_;
};

// The owner outlives this call. The scenario mutates Input as it observes a
// Stop or creates a render context; a throw keeps those recorded facts. The
// returned Report tells the owner whether it must retain resources or hand an
// uncertain final destruction to the existing outer containment.
template<class Scenario>
Report runScenario(Coordinator& coordinator, Input input, Operations& ops, Scenario&& scenario) noexcept
{
  if (!coordinator.beginScenario()) return coordinator.currentReport();
  const auto deadline = input.deadline;
  try { input.originalVerdict = std::forward<Scenario>(scenario)(input); }
  catch (...) { input.originalVerdict = false; input.scenarioException = true; }
  input.deadline = deadline;
  coordinator.endScenario();
  return coordinator.run(input, ops);
}
} // namespace HealthyCleanup
