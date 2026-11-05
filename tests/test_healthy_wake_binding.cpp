#include "HealthyWakeBinding.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QObject>
#include <QMetaObject>
#include <QSemaphore>
#include <QThread>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "check failed at line %d\n", __LINE__); std::abort(); } } while (false)
using Binding = HealthyWakeBinding;
using Step = HealthyCleanup::Step;

struct NativeSetter {
  Binding::NativeCallback callback = nullptr;
  void* context = nullptr;
  bool callDuringUnregister = false;
  int installs = 0, unregisters = 0;
  void operator()(Binding::NativeCallback next, void* value) {
    if (!next) {
      ++unregisters;
      if (callDuringUnregister && callback) callback(context);
    } else ++installs;
    callback = next;
    context = value;
  }
  void notify() { CHECK(callback); callback(context); }
};

static void pump() { QCoreApplication::processEvents(QEventLoop::AllEvents); }

static std::int64_t now() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void installController(Binding& binding, QObject& worker) {
  QSemaphore done;
  bool installed = false;
  CHECK(QMetaObject::invokeMethod(&worker, [&] { installed = binding.install(Binding::Role::Controller); done.release(); },
                                  Qt::QueuedConnection));
  CHECK(done.tryAcquire(1, 2000) && installed);
}

static void closeWake(Binding& binding, Binding::Key key) {
  CHECK(binding.apply(Step::CloseWakeAdmission, key, now() + 1000000000) == Binding::Result::Done);
  CHECK(binding.apply(Step::UnregisterWake, key, now() + 1000000000) == Binding::Result::Done);
  CHECK(binding.apply(Step::DrainWakeCallbacks, key, now() + 1000000000) == Binding::Result::Done);
}

static void callbackHandoffAndInvalidation(QObject& events, QObject& hooks, QObject& worker) {
  NativeSetter native;
  std::vector<Binding::Role> delivered;
  QSemaphore controllerDelivered;
  bool correctThread = true;
  const Binding::Key key{1, 7};
  Binding binding(&worker, &events, &hooks, &worker, key,
                  [&](auto callback, auto context) { native(callback, context); },
                  [&](auto role) { delivered.push_back(role); if (role == Binding::Role::Controller) {
                    correctThread = QThread::currentThread() == worker.thread(); controllerDelivered.release();
                  } else correctThread = QThread::currentThread() == events.thread(); },
                  now, Binding::NativeBarrier::SingleSlotSerialized);
  installController(binding, worker);
  native.notify();
  CHECK(controllerDelivered.tryAcquire(1, 1000));
  CHECK(correctThread && delivered.size() == 1 && delivered[0] == Binding::Role::Controller);
  native.notify();
  CHECK(binding.install(Binding::Role::Player));
  CHECK(controllerDelivered.tryAcquire(1, 1000));
  CHECK(correctThread && delivered.size() == 2 && delivered[1] == Binding::Role::Controller);
  native.notify();
  pump();
  CHECK(correctThread && delivered.size() == 3 && delivered[2] == Binding::Role::Player);
  native.notify();
  closeWake(binding, key);
  CHECK(binding.apply(Step::InvalidatePlayerEvents, key, now() + 1000000000) == Binding::Result::Done);
  pump();
  CHECK(delivered.size() == 3);
}

static void closureAndDelayedHook(QObject& events, QObject& hooks, QObject& worker) {
  NativeSetter native;
  int deliveries = 0, hookCount = 0;
  const Binding::Key key{2, 8};
  Binding binding(&worker, &events, &hooks, &worker, key,
                  [&](auto callback, auto context) { native(callback, context); },
                  [&](auto) { ++deliveries; }, now, Binding::NativeBarrier::SingleSlotSerialized);
  installController(binding, worker);
  CHECK(binding.install(Binding::Role::Player));
  CHECK(binding.postHook(0, [&] { ++hookCount; }));
  native.callDuringUnregister = true;
  CHECK(binding.apply(Step::CloseWakeAdmission, {key.owner, key.generation + 1}, now() + 1000000000) == Binding::Result::Blocked);
  closeWake(binding, key);
  CHECK(binding.apply(Step::UnregisterWake, key, now() + 1000000000) == Binding::Result::Blocked);
  CHECK(binding.apply(Step::InvalidateHooks, key, now() + 1000000000) == Binding::Result::Done);
  CHECK(binding.apply(Step::InvalidatePlayerEvents, key, now() + 1000000000) == Binding::Result::Done);
  CHECK(binding.apply(Step::FenceWorker, key, now() + 1000000000) == Binding::Result::Done);
  pump();
  CHECK(deliveries == 0 && hookCount == 0);
  CHECK(native.unregisters == 1);
}

static void copiedHookArguments(QObject& events, QObject& hooks, QObject& worker) {
  NativeSetter native;
  int observed = 0;
  const Binding::Key key{3, 9};
  Binding binding(&worker, &events, &hooks, &worker, key,
                  [&](auto callback, auto context) { native(callback, context); },
                  [](auto) {}, now, Binding::NativeBarrier::SingleSlotSerialized);
  installController(binding, worker);
  {
    int source = 42;
    CHECK(binding.postHook(0, [copy = source, &observed] { observed = copy; }));
    source = 99;
  }
  pump();
  CHECK(observed == 42);
  closeWake(binding, key);
}

static void workerFences(QObject& events, QObject& hooks) {
  QThread thread;
  auto* worker = new QObject;
  worker->moveToThread(&thread);
  QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
  thread.start();
  NativeSetter native;
  const Binding::Key key{4, 10};
  {
    Binding binding(worker, &events, &hooks, worker, key,
                    [&](auto callback, auto context) { native(callback, context); }, [](auto) {}, now,
                    Binding::NativeBarrier::SingleSlotSerialized);
    installController(binding, *worker);
    closeWake(binding, key);
    CHECK(binding.apply(Step::InvalidatePlayerEvents, key, now() + 1000000000) == Binding::Result::Done);
    CHECK(binding.apply(Step::InvalidateHooks, key, now() + 1000000000) == Binding::Result::Done);
    CHECK(binding.apply(Step::FenceWorker, key, now() + 1000000000) == Binding::Result::Done);
    QSemaphore entered, release, done;
    CHECK(QMetaObject::invokeMethod(worker, [&] { entered.release(); release.tryAcquire(1, 3000); done.release(); }, Qt::QueuedConnection));
    CHECK(entered.tryAcquire(1, 1000));
    CHECK(binding.apply(Step::FenceWorker, key, now() + 30000000) == Binding::Result::Blocked);
    release.release();
    CHECK(done.tryAcquire(1, 1000));
  }
  thread.quit();
  CHECK(thread.wait(4000));
}

static void unqualifiedBarrierRetainsOwner(QObject& events, QObject& hooks, QObject& worker) {
  NativeSetter native;
  const Binding::Key key{5, 11};
  auto binding = std::make_unique<Binding>(&worker, &events, &hooks, &worker, key,
                  [&](auto callback, auto context) { native(callback, context); }, [](auto) {}, now,
                  Binding::NativeBarrier::Unqualified);
  installController(*binding, worker);
  CHECK(binding->apply(Step::CloseWakeAdmission, key, now() + 1000000000) == Binding::Result::Done);
  CHECK(binding->apply(Step::UnregisterWake, key, now() + 1000000000) == Binding::Result::Blocked);
  CHECK(binding->apply(Step::DrainWakeCallbacks, key, now() + 1000000000) == Binding::Result::Blocked);
  CHECK(binding->apply(Step::UnregisterWake, key, now() + 1000000000) == Binding::Result::Blocked);
  CHECK(native.unregisters == 1 && binding != nullptr);
  // The injected setter has removed the callback; only this fake permits release.
  CHECK(native.callback == nullptr);
}

static void heldBeforeCallbackEntry(QObject& events, QObject& hooks, QObject& worker) {
  std::mutex nativeLock;
  Binding::NativeCallback callback = nullptr;
  void* context = nullptr;
  int delivered = 0;
  const Binding::Key key{6, 12};
  Binding binding(&worker, &events, &hooks, &worker, key, [&](auto next, auto value) {
    std::lock_guard<std::mutex> guard(nativeLock);
    callback = next; context = value;
  }, [&](auto) { ++delivered; }, now, Binding::NativeBarrier::SingleSlotSerialized);
  installController(binding, worker);
  QSemaphore acquired;
  std::thread producer([&] {
    std::lock_guard<std::mutex> guard(nativeLock);
    acquired.release();
    QThread::msleep(50);
    callback(context);
  });
  CHECK(acquired.tryAcquire(1, 1000));
  closeWake(binding, key);
  producer.join();
  pump();
  CHECK(delivered == 0);
}

static void setterFailureRejectsQueuedDelivery(QObject& events, QObject& hooks, QObject& worker) {
  NativeSetter native;
  int setters = 0, delivered = 0;
  const Binding::Key key{7, 13};
  Binding binding(&worker, &events, &hooks, &worker, key, [&](auto callback, auto context) {
    if (++setters == 2) throw 1;
    native(callback, context);
  }, [&](auto) { ++delivered; }, now, Binding::NativeBarrier::SingleSlotSerialized);
  installController(binding, worker);
  QSemaphore entered, release, finished, drained;
  CHECK(QMetaObject::invokeMethod(&worker, [&] { entered.release(); release.tryAcquire(1, 3000); finished.release(); }, Qt::QueuedConnection));
  CHECK(entered.tryAcquire(1, 1000));
  native.notify();
  CHECK(!binding.install(Binding::Role::Player));
  release.release();
  CHECK(finished.tryAcquire(1, 1000));
  CHECK(QMetaObject::invokeMethod(&worker, [&] { drained.release(); }, Qt::QueuedConnection));
  CHECK(drained.tryAcquire(1, 1000));
  CHECK(delivered == 0);
  CHECK(binding.apply(Step::CloseWakeAdmission, key, now() + 1000000000) == Binding::Result::Blocked);
  // Fake-only cleanup: this setter has no native producer after clearing its slot.
  native(nullptr, nullptr);
}

static void oldControllerDeliveryIsInvalidated(QObject& events, QObject& hooks, QObject& worker) {
  NativeSetter native;
  int delivered = 0;
  const Binding::Key key{8, 14};
  auto binding = std::make_unique<Binding>(&worker, &events, &hooks, &worker, key,
    [&](auto callback, auto context) { native(callback, context); }, [&](auto) { ++delivered; },
    now, Binding::NativeBarrier::SingleSlotSerialized);
  installController(*binding, worker);
  QSemaphore entered, release, finished;
  CHECK(QMetaObject::invokeMethod(&worker, [&] { entered.release(); release.tryAcquire(1, 3000); finished.release(); }, Qt::QueuedConnection));
  CHECK(entered.tryAcquire(1, 1000));
  native.notify();
  CHECK(binding->install(Binding::Role::Player));
  closeWake(*binding, key);
  CHECK(binding->apply(Step::InvalidatePlayerEvents, key, now() + 1000000000) == Binding::Result::Done);
  CHECK(binding->apply(Step::InvalidateHooks, key, now() + 1000000000) == Binding::Result::Done);
  CHECK(binding->apply(Step::FenceWorker, key, now() + 30000000) == Binding::Result::Blocked);
  CHECK(binding != nullptr);
  release.release();
  CHECK(finished.tryAcquire(1, 1000));
  // Fake-test settlement only; a failed coordinator is never authorized to retry.
  CHECK(binding->apply(Step::FenceWorker, key, now() + 1000000000) == Binding::Result::Done);
  CHECK(delivered == 0);
}


int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  QObject events, hooks;
  QThread thread;
  auto* worker = new QObject;
  worker->moveToThread(&thread);
  QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
  thread.start();
  callbackHandoffAndInvalidation(events, hooks, *worker);
  closureAndDelayedHook(events, hooks, *worker);
  copiedHookArguments(events, hooks, *worker);
  workerFences(events, hooks);
  unqualifiedBarrierRetainsOwner(events, hooks, *worker);
  heldBeforeCallbackEntry(events, hooks, *worker);
  setterFailureRejectsQueuedDelivery(events, hooks, *worker);
  oldControllerDeliveryIsInvalidated(events, hooks, *worker);
  thread.quit();
  CHECK(thread.wait(4000));
}
