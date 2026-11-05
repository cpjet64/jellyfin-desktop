#include "HealthyWakeBinding.h"
#include <QMetaObject>
#include <QSemaphore>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <limits>
#include <utility>

struct HealthyWakeBinding::State {
  QObject* controller;
  QObject* player;
  QObject* hooks;
  QObject* worker;
  Key key;
  Delivery delivery;
  std::atomic<bool> admission{true}, controllerValid{true}, eventsValid{true}, hooksValid{true}, failed{false};
  std::atomic<unsigned> active{0};
  // Control operations fail fast on overlap; wake admission never uses this flag.
  std::atomic_flag transition = ATOMIC_FLAG_INIT;
  bool attempted = false, unregisterAttempted = false, unregistered = false, drained = false;
  bool handoffAttempted = false;
  NativeBarrier barrier = NativeBarrier::Unqualified;
  unsigned installed = 0;
};

struct HealthyWakeBinding::Endpoint { std::shared_ptr<State> state; Role role; };

namespace {
struct Transition {
  std::atomic_flag& flag;
  bool acquired;
  explicit Transition(std::atomic_flag& value) : flag(value), acquired(!flag.test_and_set()) {}
  ~Transition() { if (acquired) flag.clear(); }
};
}

HealthyWakeBinding::HealthyWakeBinding(QObject* controller, QObject* player, QObject* hooks, QObject* worker,
    Key key, Setter setter, Delivery delivery, Clock clock, NativeBarrier barrier)
  : state_(std::make_shared<State>()), setter_(std::move(setter)), clock_(std::move(clock))
{
  state_->controller = controller; state_->player = player; state_->hooks = hooks; state_->worker = worker;
  state_->key = key; state_->delivery = std::move(delivery); state_->barrier = barrier;
}

HealthyWakeBinding::~HealthyWakeBinding() = default;

bool HealthyWakeBinding::onOwnerThread() const noexcept
{
  return state_->controller && state_->player && state_->hooks && state_->worker && state_->key.owner &&
    state_->key.generation && state_->controller->thread() == state_->worker->thread() &&
    state_->player->thread() == QThread::currentThread() && state_->hooks->thread() == QThread::currentThread();
}

bool HealthyWakeBinding::install(Role role) noexcept
{
  Transition transition(state_->transition);
  if (!transition.acquired) return false;
  try {
    if (role != Role::Controller) return false;
    const unsigned bit = 1U;
    QObject* target = state_->controller;
    if (!target || !state_->worker || !state_->controller || !state_->player || !state_->hooks ||
        !state_->key.owner || !state_->key.generation || state_->controller->thread() != state_->worker->thread() ||
        target->thread() != QThread::currentThread() || !setter_ || !state_->delivery || !state_->admission ||
        state_->failed || (state_->installed & bit)) return false;
    auto endpoint = std::make_unique<Endpoint>();
    endpoint->state = state_; endpoint->role = role;
    auto* raw = endpoint.get();
    endpoints_.push_back(std::move(endpoint));
    state_->attempted = true;
    setter_(&HealthyWakeBinding::wake, raw);
    state_->installed |= bit;
    return true;
  } catch (...) { state_->failed = true; return false; }
}

HealthyWakeBinding::Result HealthyWakeBinding::handoff(Key key, std::int64_t deadline) noexcept
{
  Transition transition(state_->transition);
  if (!transition.acquired) return Result::Blocked;
  try {
    if (!onOwnerThread() || key.owner != state_->key.owner || key.generation != state_->key.generation ||
        !clock_ || clock_() >= deadline || state_->failed || !state_->admission ||
        !(state_->installed & 1) || state_->handoffAttempted) return Result::Blocked;
    state_->handoffAttempted = true;
    state_->controllerValid = false;
    if (state_->barrier != NativeBarrier::SingleSlotSerialized || fence(deadline) != Result::Done ||
        state_->failed || clock_() >= deadline) { state_->failed = true; return Result::Blocked; }
    auto endpoint = std::make_unique<Endpoint>();
    endpoint->state = state_; endpoint->role = Role::Player;
    auto* raw = endpoint.get();
    endpoints_.push_back(std::move(endpoint));
    setter_(&HealthyWakeBinding::wake, raw);
    if (clock_() >= deadline || !postDelivery(state_, Role::Player) || clock_() >= deadline) {
      state_->failed = true;
      return Result::Blocked;
    }
    state_->installed |= 2U;
    return Result::Done;
  } catch (...) { state_->failed = true; return Result::Blocked; }
}

bool HealthyWakeBinding::postDelivery(const std::shared_ptr<State>& state, Role role)
{
  const auto key = state->key;
  QObject* target = role == Role::Controller ? state->controller : state->player;
  return QMetaObject::invokeMethod(target, [state, key, role] {
    if (state->failed.load() || !state->eventsValid.load() ||
        (role == Role::Controller && !state->controllerValid.load()) ||
        key.owner != state->key.owner || key.generation != state->key.generation) return;
    try { state->delivery(role); } catch (...) { state->failed = true; }
  }, Qt::QueuedConnection);
}

void HealthyWakeBinding::wake(void* context) noexcept
{
  auto* endpoint = static_cast<Endpoint*>(context);
  auto state = endpoint->state;
  ++state->active;
  try {
    if (state->admission.load() && !state->failed.load() &&
        (endpoint->role != Role::Controller || state->controllerValid.load())) {
      if (!postDelivery(state, endpoint->role)) state->failed = true;
    }
  } catch (...) { state->failed = true; }
  --state->active;
}

bool HealthyWakeBinding::postHook(int delayMs, std::function<void()> action) noexcept
{
  try {
    if (!onOwnerThread() || delayMs < 0 || !action || !state_->hooksValid || state_->failed) return false;
    const auto state = state_; const auto key = state->key;
    QTimer::singleShot(delayMs, state->hooks, [state, key, action = std::move(action)] {
      if (state->failed.load() || !state->hooksValid.load() || key.owner != state->key.owner || key.generation != state->key.generation) return;
      try { action(); } catch (...) { state->failed = true; }
    });
    return true;
  } catch (...) { state_->failed = true; return false; }
}

HealthyWakeBinding::Result HealthyWakeBinding::apply(HealthyCleanup::Step step, Key key, std::int64_t deadline) noexcept
{
  using Step = HealthyCleanup::Step;
  if (step != Step::CloseWakeAdmission && step != Step::UnregisterWake && step != Step::DrainWakeCallbacks &&
      step != Step::InvalidatePlayerEvents && step != Step::InvalidateHooks && step != Step::FenceWorker)
    return Result::Unsupported;
  Transition transition(state_->transition);
  if (!transition.acquired) return Result::Blocked;
  try {
    if (!onOwnerThread() || key.owner != state_->key.owner || key.generation != state_->key.generation ||
        !clock_ || clock_() >= deadline || state_->failed) return Result::Blocked;
    if (step == Step::CloseWakeAdmission) { state_->admission = false; return Result::Done; }
    if (step == Step::UnregisterWake) {
      if (state_->admission || !state_->attempted || state_->unregisterAttempted) return Result::Blocked;
      state_->unregisterAttempted = true;
      setter_(nullptr, nullptr); // Native barrier: no admission mutex is held here.
      if (state_->barrier != NativeBarrier::SingleSlotSerialized) return Result::Blocked;
      state_->unregistered = true;
      return clock_() < deadline ? Result::Done : Result::Blocked;
    }
    if (step == Step::DrainWakeCallbacks) {
      if (!state_->unregistered || state_->active.load() != 0) return Result::Blocked;
      state_->drained = true;
      return Result::Done;
    }
    if (!state_->drained) return Result::Blocked;
    if (step == Step::InvalidatePlayerEvents) { state_->eventsValid = false; return Result::Done; }
    if (step == Step::InvalidateHooks) { state_->hooksValid = false; return Result::Done; }
    if (state_->eventsValid || state_->hooksValid) return Result::Blocked;
    const auto result = fence(deadline);
    return state_->failed ? Result::Blocked : result;
  } catch (...) { state_->failed = true; return Result::Blocked; }
}

HealthyWakeBinding::Result HealthyWakeBinding::fence(std::int64_t deadline) noexcept
{
  try {
    QThread* thread = state_->worker->thread();
    if (!thread || thread == QThread::currentThread() || !thread->isRunning()) return Result::Blocked;
    auto completed = std::make_shared<QSemaphore>();
    if (!QMetaObject::invokeMethod(state_->worker, [completed] { completed->release(); }, Qt::QueuedConnection))
      return Result::Blocked;
    const auto now = clock_();
    if (now >= deadline) return Result::Blocked;
    const auto remaining = static_cast<std::uint64_t>(deadline) - static_cast<std::uint64_t>(now);
    const auto ms = remaining / 1000000U;
    const auto limit = static_cast<std::uint64_t>(std::numeric_limits<int>::max());
    const int wait = static_cast<int>(ms > limit ? limit : ms);
    return completed->tryAcquire(1, wait) && clock_() < deadline ? Result::Done : Result::Blocked;
  } catch (...) { state_->failed = true; return Result::Blocked; }
}
