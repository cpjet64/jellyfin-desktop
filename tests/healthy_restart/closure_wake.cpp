#include "closure_wake.h"
#include <utility>

namespace HealthyRetirement {
WakeClosure::WakeClosure(std::uint64_t generation, Delivery delivery)
  : generation_(generation), delivery_(std::move(delivery)) {}

void WakeClosure::callback(void* context) noexcept
{
  auto* self = static_cast<WakeClosure*>(context);
  if (!self) return;
  self->active_.fetch_add(1);
  try {
    if (self->generation_ && self->open_.load() && self->delivery_)
      self->delivery_(self->generation_);
  } catch (...) {
    self->failed_.store(true);
    self->open_.store(false);
  }
  self->active_.fetch_sub(1);
}

void WakeClosure::closeAdmission() noexcept { open_.store(false); }


bool WakeClosure::unregisterOnce(const std::function<void()>& unregister,
                                 Barrier barrier) noexcept
{
  closeAdmission();
#ifdef HEALTHY_RETIREMENT_TESTING
  if (barrier != Barrier::TestSingleSlot) return false;
  if (!unregister || attempted_.exchange(true)) return false;
  try { unregister(); } catch (...) { return false; }
  unregistered_.store(true);
  return true;
#else
  (void)barrier;
  (void)unregister;
  return false; // Native qualification requires the actual module-bound capability.
#endif
}

bool WakeClosure::drained() const noexcept
{
  return unregistered_.load() && !open_.load() && !failed_.load() && active_.load() == 0;
}

bool WakeClosure::deliveryAllowed(std::uint64_t generation) const noexcept
{
  return generation && generation == generation_ && open_.load() && !failed_.load();
}
}
