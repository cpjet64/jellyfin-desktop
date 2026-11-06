#include "healthy_restart/owner.h"
#include <atomic>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <thread>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "check failed at line %d\n", __LINE__); std::abort(); } } while (false)
using namespace HealthyRetirement;

struct Shared {
  std::atomic<std::int64_t> now{1};
  int context = 0;
  std::atomic<int> destroyed{0}, frees{0}, approvals{0};
  bool workerClosed = false;
  std::function<void()> onWorker, onApprove;
};

class OwnedGraph final : public Graph {
public:
  explicit OwnedGraph(std::shared_ptr<Shared> state) : state_(std::move(state)) {}
  ~OwnedGraph() override { ++state_->destroyed; }
  bool eventWorkerClosed() const override { if (state_->onWorker) state_->onWorker(); return state_->workerClosed; }
  bool approveDestruction() override { if (state_->onApprove) state_->onApprove(); ++state_->approvals; return true; }
private:
  std::shared_ptr<Shared> state_;
};

struct Fixture {
  std::shared_ptr<Shared> state = std::make_shared<Shared>();
  std::shared_ptr<Containment> lease = std::make_shared<Containment>();
  std::shared_ptr<Slot> slot;
  std::shared_ptr<Lifetime> lifetime;
  std::shared_ptr<int> token = std::make_shared<int>(7);
  std::vector<std::shared_ptr<RenderJob>> jobs;
  Fixture()
  {
    const auto shared = state;
    const Identity identity{11, 22, 33};
    Operations ops{[shared] { return shared->now.load(); }, [=] { return identity; }, [](void*) {},
      [] { return BarrierResult::Ready; }, [](std::uint64_t) { return true; },
      [shared](void*) { ++shared->frees; }, [](void*) { return true; }};
    slot = std::make_shared<Slot>(7, 100, [shared] { return shared->now.load(); });
    CHECK(slot->beginCreation() && slot->beforeCreate() && slot->recordCreated(&state->context));
    lifetime = slot->install(identity, &state->context, ops);
    CHECK(lifetime);
  }
  std::shared_ptr<Owner> owner(Owner::Schedule schedule)
  {
    const auto shared = state;
    std::unique_ptr<Graph> graph = std::make_unique<OwnedGraph>(shared);
    CHECK(lease->adopt(graph, slot, token) && !graph);
    return std::make_shared<Owner>(lease, 100,
      [shared] { return shared->now.load(); }, std::move(schedule));
  }
  bool schedule(std::shared_ptr<RenderJob> job) { jobs.push_back(std::move(job)); return true; }
#ifdef HEALTHY_RETIREMENT_TESTING
  std::shared_ptr<Owner> testOwner(std::function<void()> onFree = {})
  {
    const auto shared = state;
    const Identity identity{11, 22, 33};
    Operations ops{[shared] { return shared->now.load(); }, [=] { return identity; }, [](void*) {},
      [] { return BarrierResult::Ready; }, [](std::uint64_t) { return true; },
      [shared, onFree](void*) { ++shared->frees; if (onFree) onFree(); }, [](void*) { return true; }};
    lifetime = std::make_shared<Lifetime>(7, identity, &state->context, 100, ops, Barrier::TestQualified);
    std::unique_ptr<Graph> graph = std::make_unique<OwnedGraph>(shared);
    CHECK(lease->adopt(graph, slot, token) && !graph);
    return std::make_shared<Owner>(lease, 100,
      [shared] { return shared->now.load(); }, [this](auto job) { return schedule(std::move(job)); }, lifetime);
  }
#endif
};

static void defaultNativeBarrierCannotRelease()
{
  Fixture f;
  auto owner = f.owner([&](auto job) { return f.schedule(std::move(job)); });
  CHECK(owner->request() && f.jobs.size() == 1);
  CHECK(!f.lifetime->mayDestroy(7, true) && owner->retained());
  f.jobs.front()->run();
  CHECK(f.state->frees == 0 && !f.lifetime->ack(7));
  CHECK(!owner->acceptAndRelease() && owner->retained());
  CHECK(f.state->approvals == 0 && f.state->destroyed == 0);
}

static void droppedOrRejectedJobRetainsGraph()
{
  Fixture dropped;
  auto owner = dropped.owner([&](auto job) { return dropped.schedule(std::move(job)); });
  CHECK(owner->request() && dropped.jobs.size() == 1);
  dropped.jobs.clear();
  owner->poll();
  CHECK(owner->retained() && !owner->acceptAndRelease());
  CHECK(dropped.state->frees == 0 && !dropped.lifetime->ack(7));
  std::weak_ptr<int> token = dropped.token;
  std::weak_ptr<Slot> slot = dropped.slot;
  std::weak_ptr<Lifetime> lifetime = dropped.lifetime;
  dropped.token.reset(); owner.reset(); dropped.lease.reset();
  dropped.slot.reset(); dropped.lifetime.reset();
  CHECK(!token.expired() && !slot.expired() && !lifetime.expired() && dropped.state->destroyed == 0);

  Fixture rejected;
  auto other = rejected.owner([](auto) { return false; });
  CHECK(!other->request() && other->retained());
  CHECK(!other->acceptAndRelease() && rejected.state->frees == 0);
}

static void adoptionTransfersOnlyOnSuccess()
{
  Fixture f;
  std::unique_ptr<Graph> graph = std::make_unique<OwnedGraph>(f.state);
  CHECK(!f.lease->adopt(graph, {}, f.token) && graph);
  CHECK(f.lease->adopt(graph, f.slot, f.token) && !graph);
  std::unique_ptr<Graph> other = std::make_unique<OwnedGraph>(f.state);
  CHECK(!f.lease->adopt(other, f.slot, f.token) && other);
}

static void deadlineAndInvalidationAreSticky()
{
  Fixture f;
  auto owner = f.owner([&](auto job) { return f.schedule(std::move(job)); });
  CHECK(owner->request());
  f.state->now = 100;
  f.jobs.front()->run();
  CHECK(owner->retained() && !owner->acceptAndRelease());
  CHECK(f.state->frees == 0 && !f.lifetime->ack(7));
  f.state->now = 1;
  owner->poll();
  CHECK(owner->retained() && !owner->acceptAndRelease());

  Fixture invalid;
  auto other = invalid.owner([&](auto job) { return invalid.schedule(std::move(job)); });
  CHECK(other->request());
  other->invalidate();
  invalid.jobs.front()->run();
  CHECK(other->retained() && !other->acceptAndRelease() && invalid.state->frees == 0);
}

static void fakeBarrierRequiresOwnerAndWorkerAcknowledgement()
{
#ifdef HEALTHY_RETIREMENT_TESTING
  Fixture f;
  auto owner = f.testOwner();
  CHECK(owner->renderThreadIs(11) && !owner->renderThreadIs(12));
  CHECK(owner->request() && f.jobs.size() == 1);
  f.jobs.front()->run();
  CHECK(f.state->frees == 1 && f.lifetime->ack(7));
  CHECK(!owner->acceptAndRelease() && owner->retained());
  CHECK(f.state->destroyed == 0 && f.state->approvals == 0);
  f.state->workerClosed = true;
  CHECK(owner->acceptAndRelease() && !owner->retained());
  CHECK(f.state->destroyed == 1 && f.state->approvals == 1);
  CHECK(!owner->acceptAndRelease());
#endif
}

static void graphCallbacksMayReenterOwner()
{
#ifdef HEALTHY_RETIREMENT_TESTING
  Fixture f;
  auto owner = f.testOwner();
  CHECK(owner->request());
  f.jobs.front()->run();
  f.state->workerClosed = true;
  f.state->onWorker = [&] { CHECK(owner->retained()); };
  f.state->onApprove = [&] { CHECK(owner->retained()); };
  std::mutex mutex;
  std::condition_variable changed;
  bool finished = false, accepted = false;
  std::thread thread([&] {
    const bool result = owner->acceptAndRelease();
    std::lock_guard<std::mutex> lock(mutex);
    accepted = result; finished = true; changed.notify_all();
  });
  {
    std::unique_lock<std::mutex> lock(mutex);
    if (!changed.wait_for(lock, std::chrono::seconds(3), [&] { return finished; })) std::abort();
  }
  thread.join();
  CHECK(accepted && f.state->destroyed == 1);
#endif
}

static void schedulerReentryAndUnexecutedJob()
{
  Fixture f;
  std::weak_ptr<Owner> weak;
  auto owner = f.owner([&](auto job) {
    const auto current = weak.lock();
    CHECK(current && current->retained());
    return f.schedule(std::move(job));
  });
  weak = owner;
  CHECK(owner->request() && f.jobs.size() == 1);
  f.state->now = 100;
  owner->poll();
  CHECK(owner->retained() && !owner->acceptAndRelease());
  f.jobs.front()->run();
  CHECK(f.state->frees == 0 && !f.lifetime->ack(7) && f.state->destroyed == 0);
}

static void invalidationBeatsPublishedAck()
{
#ifdef HEALTHY_RETIREMENT_TESTING
  Fixture f;
  auto owner = f.testOwner();
  CHECK(owner->request());
  f.jobs.front()->run();
  CHECK(f.state->frees == 1 && f.lifetime->ack(7));
  f.state->workerClosed = true;
  owner->invalidate();
  CHECK(!owner->acceptAndRelease() && owner->retained());
  CHECK(f.state->destroyed == 0 && f.state->approvals == 0);

  Fixture reentrant;
  auto other = reentrant.testOwner();
  CHECK(other->request());
  reentrant.jobs.front()->run();
  reentrant.state->workerClosed = true;
  reentrant.state->onWorker = [&] { other->invalidate(); };
  CHECK(!other->acceptAndRelease() && other->retained());
  CHECK(reentrant.state->destroyed == 0 && reentrant.state->approvals == 0);
#endif
}

static void blockedFreeDoesNotBlockOwnerPoll()
{
#ifdef HEALTHY_RETIREMENT_TESTING
  Fixture f;
  std::mutex mutex;
  std::condition_variable changed;
  bool entered = false, release = false, polled = false;
  auto owner = f.testOwner([&] {
    std::unique_lock<std::mutex> lock(mutex);
    entered = true; changed.notify_all();
    changed.wait(lock, [&] { return release; });
  });
  CHECK(owner->request());
  std::thread render([&] { f.jobs.front()->run(); });
  {
    std::unique_lock<std::mutex> lock(mutex);
    if (!changed.wait_for(lock, std::chrono::seconds(3), [&] { return entered; })) std::abort();
  }
  std::thread gui([&] {
    owner->poll();
    std::lock_guard<std::mutex> lock(mutex);
    polled = true; changed.notify_all();
  });
  bool timely;
  {
    std::unique_lock<std::mutex> lock(mutex);
    timely = changed.wait_for(lock, std::chrono::seconds(2), [&] { return polled; });
    release = true; changed.notify_all();
  }
  render.join(); gui.join();
  CHECK(timely && f.state->frees == 1);
#endif
}

static void concurrentOwnersCannotShareLease()
{
#ifdef HEALTHY_RETIREMENT_TESTING
  Fixture f;
  std::unique_ptr<Graph> graph = std::make_unique<OwnedGraph>(f.state);
  CHECK(f.lease->adopt(graph, f.slot, f.token) && !graph);
  const auto shared = f.state;
  const Identity identity{11, 22, 33};
  Operations ops{[shared] { return shared->now.load(); }, [=] { return identity; }, [](void*) {},
    [] { return BarrierResult::Ready; }, [](std::uint64_t) { return true; },
    [shared](void*) { ++shared->frees; }, [](void*) { return true; }};
  const auto lifetime = std::make_shared<Lifetime>(7, identity, &f.state->context, 100, ops, Barrier::TestQualified);
  std::array<std::shared_ptr<Owner>, 2> owners;
  std::atomic<int> ready{0};
  std::atomic<bool> go{false};
  auto construct = [&](int index) {
    ++ready;
    while (!go.load()) std::this_thread::yield();
    owners[index] = std::make_shared<Owner>(f.lease, 100, [shared] { return shared->now.load(); },
      [&f](auto job) { return f.schedule(std::move(job)); }, lifetime);
  };
  std::thread first(construct, 0), second(construct, 1);
  while (ready.load() != 2) std::this_thread::yield();
  go = true;
  first.join(); second.join();
  const bool a = owners[0]->request();
  const bool b = owners[1]->request();
  CHECK(a != b && f.lease->intact() && f.jobs.size() == 1);
  owners[a ? 1 : 0]->invalidate();
  owners[a ? 1 : 0]->poll();
  CHECK(!owners[a ? 1 : 0]->acceptAndRelease() && f.lease->intact());
  owners[a ? 1 : 0].reset();
  CHECK(f.lease->intact());
  f.jobs.front()->run();
  f.state->workerClosed = true;
  CHECK(owners[a ? 0 : 1]->acceptAndRelease());
  CHECK(f.state->frees == 1 && f.state->destroyed == 1);
#endif
}

int main()
{
  defaultNativeBarrierCannotRelease();
  droppedOrRejectedJobRetainsGraph();
  deadlineAndInvalidationAreSticky();
  fakeBarrierRequiresOwnerAndWorkerAcknowledgement();
  graphCallbacksMayReenterOwner();
  schedulerReentryAndUnexecutedJob();
  invalidationBeatsPublishedAck();
  blockedFreeDoesNotBlockOwnerPoll();
  adoptionTransfersOnlyOnSuccess();
  concurrentOwnersCannotShareLease();
}
