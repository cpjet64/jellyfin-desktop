#pragma once
#include "async_stop.h"
#include "lifetime_qt.h"
#include "owner.h"
#include "failure_cause.h"
#include "fixture_restart/RestartChild.h"
#include <QPointer>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#ifdef HEALTHY_RETIREMENT_TESTING
#include "module_gate.h"
#endif

class MpvVideoItem;
class PlayerComponent;
class QQuickWindow;
class QThread;
class QByteArray;
class QString;
#ifdef HEALTHY_RETIREMENT_TESTING
class TestHealthyGraphArm;
class TestHealthyGraphCapture;
class TestHealthyGraphLifetime;
#endif
namespace HealthyModuleIdentity { class VerifiedModule; class ChildEndpoints; }

namespace HealthyRetirement {
#ifdef HEALTHY_RETIREMENT_TESTING
void setGraphInstallHookForTest(std::function<bool()> hook);
void setGraphPreArmHookForTest(std::function<void()> hook);
void setGraphBindHookForTest(std::function<void()> hook);
#endif
class Closure;
class NativeTeardownState;
class QtScheduler;
class FixtureGraph;
class GraphRetirement;
class GraphAccessOps;
class CaptureController;
class ChildSession;
enum class AccessPhase { Normal, CleanupOnly, Closed, Contained };
struct ClosedAck {
  const std::uint64_t generation, coreGeneration, closureGeneration, revision, attempt;
  const std::int64_t completedAt;
};

// Only the graph writes this pointer; queued work reads it on the GUI thread.
// The access record remains alive after the graph itself has been destroyed.
struct GraphAccess {
  std::atomic<AccessPhase> phase{AccessPhase::Normal};
  std::atomic<FixtureGraph*> graph{nullptr};
  std::thread::id guiThread;
  QThread* guiQtThread = nullptr;
  std::function<void(FixtureGraph*)> guiInvalidate;
  std::atomic_bool revoked{false};
  std::shared_ptr<RetirementClaim> intentClaim;
  std::weak_ptr<HealthyModuleIdentity::ChildEndpoints> endpoints;
  std::mutex mutex;
  std::optional<ClosedAck> closed;
  std::function<bool(std::uint64_t)> capability;
  std::shared_ptr<const RetirementClaim> stopClaim;
  Owner::Clock clock;
  std::int64_t deadline = 0;
};

class PreparedGraph {
public:
  enum class FailureProgress { Pending, Qualified, Unknown };
  bool beginRestoreFailure(HealthyRestart::FailureCause cause);
  FailureProgress pollRestoreFailure();
  const std::shared_ptr<Containment>& containment() const { return containment_; }
  const std::shared_ptr<Slot>& slot() const { return slot_; }
  FixtureGraph* graphOnGuiThread() const noexcept;
  std::optional<ClosedAck> closedAck() const;
  void invalidate() noexcept;
  // Begins owned construction only. Player binding and readiness are separate.
  bool begin(std::shared_ptr<HealthyModuleIdentity::ChildEndpoints> endpoints);
private:
  friend class FixtureGraph;
  friend class ChildSession;
  bool ownsEndpoints(const std::shared_ptr<HealthyModuleIdentity::ChildEndpoints>& endpoints) const noexcept;
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
#ifdef HEALTHY_RETIREMENT_TESTING
  friend class ::TestHealthyGraphArm;
  friend class ::TestHealthyGraphCapture;
  friend class ::TestHealthyGraphLifetime;
#endif
  friend class PreparedGraph;
  friend class GraphAccessOps;
  friend class ChildSession;
  class Session;
  FixtureGraph(std::shared_ptr<Slot> slot, std::shared_ptr<GraphAccess> access,
               std::int64_t normalDeadline, std::int64_t overallDeadline, Clock clock);
  bool begin(const std::shared_ptr<Containment>& containment,
             std::shared_ptr<HealthyModuleIdentity::ChildEndpoints> endpoints);
  void deliverRedraw();
  void invalidate() noexcept;
  bool sessionReady(bool source) const;
  bool cleanupSessionReady() const;
  bool failureCleanupReady() const;
  bool sourceLoad(const QString& path, bool paused);
  bool sourceFreeze(QByteArray& frame);
  bool sourceBeginStop(const QByteArray& control);
  HealthyRestart::AsyncStop::Status sourcePollStop(QByteArray& response);
  bool restoreInitialize(const QString& path);
  FixtureRestart::RestartChild::ArmResult restoreArmActual();
  FixtureRestart::RestartChild::Actions restoreActions(const QString& path, quint64 launchId, const QByteArray& digest);
  bool restoreGrant(const QByteArray& frame, bool eof, QByteArray& response);
  bool restoreControl(const QByteArray& frame, QByteArray& response);
  bool restorePollCancellation(QByteArray& response);
  bool restoreLoad();
  bool restoreSeek();
  bool captureBaseline();
  bool captureSessionReady() const;
  bool capturePlayerBound() const;
  bool captureTarget();
  // Renderer ticket acknowledgement only; target image and Player receipts
  // must be qualified separately after the future checked seek.
  bool captureTargetReady();
  void cancelCapture() noexcept;
  bool restoreRevoke();
  bool beginRestoreFailure(HealthyRestart::FailureCause cause);
  PreparedGraph::FailureProgress pollRestoreFailure();
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
  std::unique_ptr<Session> session_;
  std::unique_ptr<QtScheduler> scheduler_;
  std::shared_ptr<GraphRetirement> retirement_;
  std::shared_ptr<NativeCell> native_;
  std::shared_ptr<const HealthyModuleIdentity::VerifiedModule> module_;
  std::shared_ptr<HealthyModuleIdentity::ChildEndpoints> endpoints_;
  std::shared_ptr<Owner> owner_;
  std::unique_ptr<CaptureController> capture_;
  bool retirementRequested_ = false;
  bool presented_ = false;
  bool captureBaselineRequested_ = false;
  bool captureTargetRequested_ = false;
#ifdef HEALTHY_RETIREMENT_TESTING
  bool bindTestFinalizer(std::function<bool()> finalizer, std::function<void()> destroyedObserver);
  bool invokeTestFinalizer();
  struct TestFinalizerBinding {
    std::weak_ptr<Containment> containment;
    std::weak_ptr<Slot> slot;
    std::weak_ptr<Lifetime> lifetime;
    std::uint64_t generation = 0;
    std::thread::id thread;
    QThread* qtThread = nullptr;
    std::function<bool()> finalizer;
    bool consumed = false;
  };
  std::unique_ptr<TestFinalizerBinding> testFinalizer_;
  std::function<void()> testDestroyedObserver_;
  std::function<bool()> captureSessionReadyForTest_;
  std::optional<HealthyModuleIdentity::LaunchBinding> captureBindingForTest_;
  std::uint64_t captureLaunchForTest_ = 0;
#endif
  mutable std::int64_t lastSessionNow_ = 0;
};
}
