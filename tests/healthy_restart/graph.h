#pragma once
#include "async_stop.h"
#include "lifetime_qt.h"
#include "owner.h"
#include <QPointer>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

class MpvVideoItem;
class PlayerComponent;
class QQuickWindow;
class QThread;
namespace HealthyModuleIdentity { class VerifiedModule; class ChildEndpoints; }

namespace HealthyRetirement {
class Closure;
class NativeTeardownState;
class QtScheduler;
class FixtureGraph;
class GraphRetirement;
class GraphAccessOps;
struct ClosedAck { const std::uint64_t generation; const std::int64_t completedAt; };

// Only the graph writes this pointer; queued work reads it on the GUI thread.
// The access record remains alive after the graph itself has been destroyed.
struct GraphAccess {
  std::atomic<FixtureGraph*> graph{nullptr};
  std::thread::id guiThread;
  QThread* guiQtThread = nullptr;
  std::function<void(FixtureGraph*)> guiInvalidate;
  std::atomic_bool revoked{false};
  std::shared_ptr<RetirementClaim> intentClaim;
  std::mutex mutex;
  std::optional<ClosedAck> closed;
  std::function<bool(std::uint64_t)> capability;
  std::shared_ptr<const RetirementClaim> stopClaim;
  Owner::Clock clock;
  std::int64_t deadline = 0;
};

class PreparedGraph {
public:
  const std::shared_ptr<Containment>& containment() const { return containment_; }
  const std::shared_ptr<Slot>& slot() const { return slot_; }
  FixtureGraph* graphOnGuiThread() const noexcept;
  std::optional<ClosedAck> closedAck() const;
  void invalidate() noexcept;
  // Begins owned construction only. Player binding and readiness are separate.
  bool begin(std::shared_ptr<HealthyModuleIdentity::ChildEndpoints> endpoints);
private:
  friend class FixtureGraph;
  std::shared_ptr<Containment> containment_;
  std::shared_ptr<Slot> slot_;
  std::shared_ptr<GraphAccess> access_;
};

// Owns the real Qt graph. Item/controller/core/worker have one ownership tree:
// the window owns its content item and video item; the copied item owns its
// controller, core and worker. The graph owns the independent Player and GUI
// dispatcher. All remain in the pre-adopted containment through uncertainty.
class FixtureGraph final : public Graph {
public:
  using Clock = Owner::Clock;
  // Both deadlines are mapped once from the parent's original monotonic clock
  // to this nanosecond clock. Keep at least the 30-second cleanup reserve.
  static std::unique_ptr<PreparedGraph> prepare(std::uint64_t generation,
                                                std::int64_t normalDeadline,
                                                std::int64_t overallDeadline, Clock clock);
  ~FixtureGraph() override;
  bool eventWorkerClosed() const override;
  bool approveDestruction() override;
  // Requests presentation; readiness still requires separately observed frames.
  bool present();
  bool installRetirementClaim();
  bool requestRetirement();
  enum class Progress { Pending, Closed, Failed };
  Progress pollRetirement();
  FixtureGraph(const FixtureGraph&) = delete;
  FixtureGraph& operator=(const FixtureGraph&) = delete;
private:
  friend class PreparedGraph;
  friend class GraphAccessOps;
  FixtureGraph(std::shared_ptr<Slot> slot, std::shared_ptr<GraphAccess> access,
               std::int64_t normalDeadline, std::int64_t overallDeadline, Clock clock);
  bool begin(const std::shared_ptr<Containment>& containment,
             std::shared_ptr<HealthyModuleIdentity::ChildEndpoints> endpoints);
  void deliverRedraw();
  void invalidate() noexcept;
  struct NativeCell { std::shared_ptr<NativeTeardownState> state; };
  std::shared_ptr<Slot> slot_;
  std::shared_ptr<GraphAccess> access_;
  std::weak_ptr<Containment> containment_;
  const std::int64_t normalDeadline_;
  const std::int64_t deadline_;
  Clock clock_;
  std::unique_ptr<QObject> dispatcher_;
  std::shared_ptr<QtToken> token_;
  std::unique_ptr<QQuickWindow> window_;
  QPointer<MpvVideoItem> item_;
  std::unique_ptr<PlayerComponent> player_;
  std::unique_ptr<QtScheduler> scheduler_;
  std::shared_ptr<GraphRetirement> retirement_;
  std::shared_ptr<NativeCell> native_;
  std::shared_ptr<const HealthyModuleIdentity::VerifiedModule> module_;
  std::shared_ptr<HealthyModuleIdentity::ChildEndpoints> endpoints_;
  std::shared_ptr<Owner> owner_;
  bool retirementRequested_ = false;
  bool presented_ = false;
};
}
