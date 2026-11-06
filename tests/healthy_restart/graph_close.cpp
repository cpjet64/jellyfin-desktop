#include "graph.h"
#include "closure.h"
#include "closure_native.h"
#include "graph_retirement.h"
#include "player/MpvVideoItem.h"
#include <QThread>
#include <utility>

namespace HealthyRetirement {
#ifdef HEALTHY_RETIREMENT_TESTING
bool FixtureGraph::bindTestFinalizer(std::function<bool()> finalizer, std::function<void()> destroyedObserver)
{
  const auto containment = containment_.lock();
  const auto lifetime = slot_ ? slot_->current() : nullptr;
  if (!finalizer || !destroyedObserver || testFinalizer_ || !containment || !containment->intact() ||
      !slot_ || !slot_->generation() || !lifetime || !owner_ || !retirement_ || !access_ ||
      access_->revoked.load() || access_->graph.load() != this || access_->guiThread != std::this_thread::get_id() ||
      access_->guiQtThread != QThread::currentThread()) return false;
  testFinalizer_ = std::make_unique<TestFinalizerBinding>(TestFinalizerBinding{
      containment, slot_, lifetime, slot_->generation(), std::this_thread::get_id(), QThread::currentThread(),
      std::move(finalizer), false});
  testDestroyedObserver_ = std::move(destroyedObserver);
  return true;
}

bool FixtureGraph::invokeTestFinalizer()
{
  const auto& binding = testFinalizer_;
  if (!binding || binding->consumed || binding->thread != std::this_thread::get_id() ||
      binding->qtThread != QThread::currentThread() || !access_ || access_->revoked.load() ||
      access_->graph.load() != this || access_->guiThread != binding->thread || access_->guiQtThread != binding->qtThread ||
      !owner_ || !retirement_ || !slot_ || binding->slot.lock() != slot_ ||
      binding->generation != slot_->generation()) return false;
  const auto containment = binding->containment.lock();
  const auto lifetime = slot_->current();
  if (!containment || containment != containment_.lock() || !containment->intact() ||
      !lifetime || binding->lifetime.lock() != lifetime || !lifetime->mayDestroy(binding->generation, true)) return false;
  binding->consumed = true;
  try { return binding->finalizer(); } catch (...) { return false; }
}
#endif
bool FixtureGraph::eventWorkerClosed() const
{
  return retirement_ && retirement_->closed();
}

bool FixtureGraph::approveDestruction()
{
#ifdef HEALTHY_RETIREMENT_TESTING
  if (testFinalizer_) return eventWorkerClosed() && invokeTestFinalizer();
#endif
  return eventWorkerClosed() && native_ && native_->state && item_ &&
         item_->thread() == QThread::currentThread() &&
         item_->healthyFinalizeClosed(*retirement_->closure(), *native_->state);
}
}
