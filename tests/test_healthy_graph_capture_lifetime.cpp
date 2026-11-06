#include "graph.h"
#include "graph_retirement.h"
#include "graph_access.h"
#include "capture_capability.h"
#include "frame_capture.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QImage>
#include <QThread>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

using namespace HealthyRetirement;
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #value); std::abort(); } } while (false)

class RenderWorker {
public:
  RenderWorker() {
    thread_ = QThread::create([this] { run(); }); thread_->start();
    std::unique_lock<std::mutex> lock(mutex_);
    CHECK(cv_.wait_for(lock, std::chrono::seconds(3), [this] { return ready_; }));
  }
  ~RenderWorker() {
    { std::lock_guard<std::mutex> lock(mutex_); stop_ = true; }
    cv_.notify_all(); CHECK(thread_->wait(3000)); delete thread_;
  }
  std::uintptr_t threadId() const { return threadId_; }
  void invoke(std::function<void()> work) {
    std::unique_lock<std::mutex> lock(mutex_);
    CHECK(!pending_ && !stop_);
    work_ = std::move(work); pending_ = true; complete_ = false; cv_.notify_all();
    CHECK(cv_.wait_for(lock, std::chrono::seconds(3), [this] { return complete_; }));
  }
private:
  void run() {
    std::unique_lock<std::mutex> lock(mutex_);
    threadId_ = reinterpret_cast<std::uintptr_t>(QThread::currentThreadId());
    ready_ = true; cv_.notify_all();
    for (;;) {
      cv_.wait(lock, [this] { return pending_ || stop_; });
      if (stop_) return;
      auto work = std::move(work_); pending_ = false;
      lock.unlock(); work(); lock.lock(); complete_ = true; cv_.notify_all();
    }
  }
  std::mutex mutex_;
  std::condition_variable cv_;
  std::function<void()> work_;
  std::uintptr_t threadId_ = 0;
  bool ready_ = false, pending_ = false, complete_ = false, stop_ = false;
  QThread* thread_ = nullptr;
};

class TestCaptureCapability {
  using Bridge = CaptureBridge;
  Identity identity() const { return {reinterpret_cast<std::uintptr_t>(QThread::currentThreadId()), 12, 13}; }
  static FramebufferObservation fbo() { return {7, 160, 90, 0, 0, 0, true, true}; }
  std::shared_ptr<Bridge> bridge_;
  std::optional<Bridge::Ticket> ticket_;
public:
  explicit TestCaptureCapability(const std::shared_ptr<Slot>& slot) : bridge_(slot->captureBridge()) {}
  bool initialize() { return bridge_ && bridge_->rendererCreated(identity()) && bridge_->framebuffer(identity(), fbo()); }
  bool queue()
  {
    if (!bridge_->framebufferCurrent(identity(), fbo())) return false;
    ticket_ = bridge_->issue(identity(), fixture_restart::nextRenderSerial());
    return ticket_->request != 0;
  }
  void deliver()
  {
    CHECK(ticket_);
    QImage image(160, 90, QImage::Format_RGBA8888); image.fill(Qt::black);
    bridge_->publish(*ticket_, identity(), image, 0, 0, 0, 0);
  }
  bool deliverOldTo(TestCaptureCapability& replacement)
  {
    if (!ticket_ || !replacement.ticket_ || ticket_->renderer == replacement.ticket_->renderer ||
        !(ticket_->key == replacement.ticket_->key) || ticket_->request != replacement.ticket_->request ||
        ticket_->framebuffer != replacement.ticket_->framebuffer) return false;
    auto stale = *ticket_; stale.serial = replacement.ticket_->serial;
    QImage image(160, 90, QImage::Format_RGBA8888); image.fill(Qt::black);
    replacement.bridge_->publish(stale, identity(), image, 0, 0, 0, 0);
    return fixture_restart::takeCaptures().empty();
  }
};
class TestHealthyGraphLifetime {
  struct Scenario {
    static constexpr std::int64_t deadline = 31000000100LL;
    std::atomic<std::int64_t> now{10};
    std::atomic<bool> capability{true};
    std::shared_ptr<RenderWorker> worker = std::make_shared<RenderWorker>();
    std::shared_ptr<std::atomic_bool> revoked = std::make_shared<std::atomic_bool>(false);
    std::shared_ptr<RetirementClaim> claim = std::make_shared<RetirementClaim>(9);
    std::unique_ptr<PreparedGraph> prepared;
    FixtureGraph* graph = nullptr;
    std::shared_ptr<GraphAccess> access;
    std::shared_ptr<Lifetime> lifetime;
    std::shared_ptr<Owner> owner;
    std::shared_ptr<RenderJob> job;
    std::shared_ptr<GraphRetirement> retirement;
    std::unique_ptr<TestCaptureCapability> renderer;
    std::optional<Closure::StopKey> stop = Closure::StopKey{
        Closure::StopKey::Origin::InternalReplacement, 1, 2, 3, 4, 5, 6, 7, true};
    int context = 0, events = 0, joins = 0, destroys = 0, frees = 0;
    bool allowClose = true, schedulerBound = false;
    int finalizers = 0, destructors = 0;
    bool finalizerResult = true, observerSawNull = false, reenter = false;
    bool nestedRelease = true, race = false, raceRejected = false;
    std::atomic_bool releaseCallback{false}, callbackFinished{false}, callbackRejected{false};

    void initialize()
    {
      fixture_restart::disarmCapture();
      prepared = FixtureGraph::prepare(9, 100, deadline, [this] { return now.load(); });
      CHECK(prepared); graph = prepared->graphOnGuiThread(); CHECK(graph);
      graph->containment_ = prepared->containment();
      access = graph->access_;
      auto slot = prepared->slot();
      CHECK(slot->bindModuleForTest([this] { return capability.load(); }, revoked));
      CHECK(slot->bindStopClaimForTest(claim));
      CHECK(slot->bindStopClaimForTest(claim));
      CHECK(!slot->bindStopClaimForTest(std::make_shared<RetirementClaim>(9)));
      CHECK(slot->beginCreation() && slot->beforeCreate() && slot->recordCreated(&context));
      Operations ops;
      ops.currentIdentity = [] { return Identity{reinterpret_cast<std::uintptr_t>(QThread::currentThreadId()), 12, 13}; };
      ops.unregisterCallback = [](void*) {};
      ops.invalidateQueued = [](std::uint64_t) { return true; };
      ops.freeContext = [this](void*) {
        CHECK(reinterpret_cast<std::uintptr_t>(QThread::currentThreadId()) == worker->threadId());
        ++frees;
      };
      ops.clearPointer = [](void*) { return true; };
      CHECK(worker->threadId() != reinterpret_cast<std::uintptr_t>(QThread::currentThreadId()));
      lifetime = slot->install({worker->threadId(), 12, 13}, &context, ops); CHECK(lifetime);
      renderer = std::make_unique<TestCaptureCapability>(slot);
      worker->invoke([this] { CHECK(renderer->initialize()); });
      CHECK(graph->capture_->request({1, 2, 3, 4, 5, 0}, 6, CaptureBridge::Phase::Baseline, claim));
      worker->invoke([this] { CHECK(renderer->queue()); });
      owner = std::make_shared<Owner>(prepared->containment(), deadline, [this] { return now.load(); },
        [this](std::shared_ptr<RenderJob> value) { job = std::move(value); return schedulerBound; }, lifetime);
      installRetirement(slot);
      graph->owner_ = owner;
      CHECK(graph->bindTestFinalizer([this] { return finalize(); }, [this] {
        CHECK(QThread::currentThread() == QCoreApplication::instance()->thread());
        ++destructors; observerSawNull = !access->graph.load(); releaseCallback = true;
      }));
    }
    bool finalize()
    {
      ++finalizers;
      CHECK(retirement->closed() && access->graph.load() == graph && owner->retained());
      if (reenter) nestedRelease = owner->acceptAndRelease();
      if (race) {
        std::thread callback([this] {
          worker->invoke([this] { renderer->deliver(); });
          raceRejected = fixture_restart::takeCaptures().empty();
        });
        callback.join();
      }
      return finalizerResult;
    }
    void installRetirement(const std::shared_ptr<Slot>& slot)
    {
      GraphRetirement::Boundaries b;
      b.closure.now = [this] { return now.load(); };
      b.closure.settledStop = [this] { return stop; };
      b.closure.closeEvents = [this] { ++events; return allowClose ? Closure::EventResult::Closed : Closure::EventResult::Pending; };
      b.closure.quitWorker = [] {};
      b.closure.joinWorker = [this](std::int64_t) { ++joins; return true; };
      b.closure.destroyCore = [this] { ++destroys; };
      b.stopClaim = [this] { return claim; };
      b.bindStopClaim = [this, slot](std::shared_ptr<const RetirementClaim> value) {
        return value == claim && slot->bindStopClaimForTest(std::move(value));
      };
      b.capability = [this](std::uint64_t generation) { return generation == 9 && capability.load(); };
      b.bindScheduler = [this] { schedulerBound = true; };
      retirement = GraphRetirement::forTest(slot, owner, lifetime, access, deadline, std::move(b));
      graph->retirement_ = retirement;
    }
    void start()
    {
      CHECK(retirement->request() && job);
      CHECK(access->graph.load() == graph && owner->retained());
      worker->invoke([this] { job->run(); }); job.reset();
    }
    FixtureGraph::Progress poll()
    {
      QElapsedTimer timer; timer.start();
      while (timer.elapsed() < 3000) {
        const auto result = retirement->poll();
        if (result != FixtureGraph::Progress::Pending) return result;
        QThread::msleep(1);
      }
      return FixtureGraph::Progress::Pending;
    }
  };
  static void qualifiedDestruction()
  {
    Scenario s; s.initialize(); s.start();
    CHECK(s.finalizers == 0 && s.destructors == 0 && s.access->graph.load() == s.graph);
    CHECK(s.poll() == FixtureGraph::Progress::Closed);
    CHECK(s.finalizers == 1 && s.destructors == 1 && s.observerSawNull && !s.owner->retained());
    CHECK(s.frees == 1 && s.events == 1 && s.joins == 1 && s.destroys == 1);
    CHECK(!s.access->graph.load());
    s.worker->invoke([&] { s.renderer->deliver(); }); CHECK(fixture_restart::takeCaptures().empty());
    CHECK(!s.owner->acceptAndRelease() && s.finalizers == 1 && s.destructors == 1);
    CHECK(s.frees == 1 && s.events == 1 && s.joins == 1 && s.destroys == 1);
    Scenario replacement; replacement.worker = s.worker; replacement.initialize();
    replacement.worker->invoke([&] { CHECK(s.renderer->deliverOldTo(*replacement.renderer)); });
    CHECK(replacement.access->graph.load() == replacement.graph && !replacement.graph->captureTargetReady());
    replacement.worker->invoke([&] { replacement.renderer->deliver(); }); CHECK(fixture_restart::takeCaptures().size() == 1);
  }
  static void incompleteClosureRetainsGraph()
  {
    Scenario s; s.initialize(); s.allowClose = false; s.start();
    CHECK(s.retirement->poll() == FixtureGraph::Progress::Pending);
    CHECK(!s.owner->acceptAndRelease());
    CHECK(s.access->graph.load() == s.graph && s.owner->retained() && !s.finalizers && !s.destructors);
    CHECK(s.frees == 1 && s.events >= 1 && !s.joins && !s.destroys);
    s.worker->invoke([&] { s.renderer->deliver(); }); CHECK(fixture_restart::takeCaptures().empty());
  }
  static void wrongGenerationRetainsGraph()
  {
    Scenario s; s.initialize();
    CHECK(s.lifetime->retireOnRenderThread(10) == Result::Blocked && !s.lifetime->ack(10));
    s.graph->testFinalizer_->generation = 10;
    s.start(); CHECK(s.poll() == FixtureGraph::Progress::Failed);
    CHECK(s.access->graph.load() == s.graph && s.owner->retained() && !s.finalizers && !s.destructors);
    CHECK(s.frees == 1 && s.events == 1 && s.joins == 1 && s.destroys == 1);
  }
  static void rejectedFinalizerRetainsGraph()
  {
    Scenario s; s.initialize(); s.finalizerResult = false; s.start();
    CHECK(s.poll() == FixtureGraph::Progress::Failed);
    CHECK(s.finalizers == 1 && !s.destructors && s.owner->retained() && s.access->graph.load() == s.graph);
    CHECK(!s.owner->acceptAndRelease() && s.finalizers == 1);
    CHECK(s.frees == 1 && s.events == 1 && s.joins == 1 && s.destroys == 1);
  }
  static void reentrantReleaseAndQueuedDelivery()
  {
    Scenario s; s.initialize(); s.reenter = true; s.race = true; s.start();
    CHECK(s.poll() == FixtureGraph::Progress::Closed);
    CHECK(!s.nestedRelease && s.raceRejected && s.finalizers == 1 && s.destructors == 1 && s.observerSawNull);
    CHECK(s.frees == 1 && s.events == 1 && s.joins == 1 && s.destroys == 1);
  }
  static void canceledRetirementRetainsGraph()
  {
    Scenario s; s.initialize(); CHECK(s.retirement->request() && s.job);
    s.claim->revoke(); s.worker->invoke([&] { s.job->run(); }); s.job.reset();
    CHECK(s.retirement->poll() == FixtureGraph::Progress::Failed);
    CHECK(s.lifetime->facts().phase == Phase::Retained);
    CHECK(s.owner->retained() && s.access->graph.load() == s.graph && !s.finalizers && !s.destructors);
    CHECK(!s.frees && !s.events && !s.joins && !s.destroys);
    s.worker->invoke([&] { s.renderer->deliver(); }); CHECK(fixture_restart::takeCaptures().empty());
  }
  static void wrongThreadRetainsGraph()
  {
    Scenario s; s.initialize(); CHECK(s.retirement->request() && s.job);
    s.job->run(); s.job.reset();
    CHECK(s.retirement->poll() == FixtureGraph::Progress::Failed);
    CHECK(s.lifetime->facts().phase == Phase::Retained);
    CHECK(s.owner->retained() && s.access->graph.load() == s.graph);
    CHECK(!s.frees && !s.events && !s.joins && !s.destroys && !s.finalizers && !s.destructors);
  }
  static void queuedCallbackOutlivesGraph()
  {
    Scenario s; s.initialize();
    auto* renderer = s.renderer.get();
    auto* release = &s.releaseCallback;
    auto* finished = &s.callbackFinished;
    auto* rejected = &s.callbackRejected;
    auto worker = s.worker;
    std::thread callback([renderer, release, finished, rejected, worker] {
      const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(3);
      while (!release->load() && std::chrono::steady_clock::now() < limit)
        std::this_thread::yield();
      if (release->load()) {
        worker->invoke([renderer] { renderer->deliver(); });
        *rejected = fixture_restart::takeCaptures().empty();
      }
      *finished = true;
    });
    s.start(); const auto result = s.poll();
    s.releaseCallback = true;
    QElapsedTimer deadline; deadline.start();
    while (!s.callbackFinished.load() && deadline.elapsed() < 4000) QThread::msleep(1);
    CHECK(s.callbackFinished);
    callback.join();
    CHECK(result == FixtureGraph::Progress::Closed && s.destructors == 1 && s.observerSawNull);
    CHECK(s.callbackFinished && s.callbackRejected && !s.access->graph.load());
  }
  static void queuedQtDeliveryAfterDestruction()
  {
    Scenario s; s.initialize();
    auto* renderer = s.renderer.get();
    auto worker = s.worker;
    bool delivered = false, rejected = false;
    CHECK(QMetaObject::invokeMethod(QCoreApplication::instance(), [renderer, worker, &delivered, &rejected] {
      worker->invoke([renderer] { renderer->deliver(); });
      rejected = fixture_restart::takeCaptures().empty(); delivered = true;
    }, Qt::QueuedConnection));
    s.start(); CHECK(s.poll() == FixtureGraph::Progress::Closed);
    CHECK(s.destructors == 1 && s.observerSawNull && !delivered);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    CHECK(delivered && rejected && !s.access->graph.load());
  }
public:
  static void run()
  {
    qualifiedDestruction(); incompleteClosureRetainsGraph(); wrongGenerationRetainsGraph();
    rejectedFinalizerRetainsGraph(); reentrantReleaseAndQueuedDelivery(); canceledRetirementRetainsGraph();
    wrongThreadRetainsGraph();
    queuedCallbackOutlivesGraph();
    queuedQtDeliveryAfterDestruction();
  }
};

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  TestHealthyGraphLifetime::run();
}
