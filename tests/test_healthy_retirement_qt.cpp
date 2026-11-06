#include "healthy_restart/lifetime_qt.h"
#include <QCoreApplication>
#include <QEvent>
#include <QObject>
#include <cstdio>
#include <cstdlib>
#include <memory>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "check failed at line %d\n", __LINE__); std::abort(); } } while (false)
using namespace HealthyRetirement;

static void queuedWakeIsRevoked()
{
  QObject dispatcher;
  int context = 0, deliveries = 0, frees = 0;
  const Identity identity{11, 22, 33};
  Operations ops{[] { return std::int64_t{1}; }, [=] { return identity; }, [](void*) {},
    [] { return BarrierResult::Ready; }, [](std::uint64_t) { return true; },
    [&](void*) { ++frees; }, [](void*) { return true; }};
  const auto slot = std::make_shared<Slot>(7, 100, [] { return std::int64_t{1}; });
  std::shared_ptr<Lifetime> owner;
  const auto token = std::make_shared<QtToken>(slot, &dispatcher, [&] {
    CHECK(owner && owner->facts().deliveries == 1);
    ++deliveries;
  });
  QtToken::redraw(nullptr);
  QtToken::redraw(token.get());
  QCoreApplication::sendPostedEvents(&dispatcher, QEvent::MetaCall);
  CHECK(deliveries == 0);
  CHECK(slot->beginCreation());
  CHECK(slot->beforeCreate());
  CHECK(slot->recordCreated(&context));
  owner = slot->install(identity, &context, ops);
  CHECK(owner);
  QtToken::redraw(token.get());
  QCoreApplication::sendPostedEvents(&dispatcher, QEvent::MetaCall);
  CHECK(deliveries == 1);
  QtToken::redraw(token.get());
  CHECK(slot->request(7) != Result::Done);
  QCoreApplication::sendPostedEvents(&dispatcher, QEvent::MetaCall);
  CHECK(deliveries == 1);
  QtToken::redraw(token.get());
  QCoreApplication::sendPostedEvents(&dispatcher, QEvent::MetaCall);
  CHECK(deliveries == 1);
  CHECK(owner->retireOnRenderThread(7) != Result::Done);
  CHECK(!owner->ack(7) && frees == 0);
  const auto failed = std::make_shared<Slot>(8, 100, [] { return std::int64_t{1}; });
  const auto badToken = std::make_shared<QtToken>(failed, &dispatcher, [] {});
  CHECK(failed->beginCreation() && failed->beforeCreate() && failed->recordCreated(&context));
  CHECK(!badToken->watchContext(nullptr));
  failed->creationFailed();
  CHECK(failed->hasContextRecord() && !failed->current());
  CHECK(failed->request(8) != Result::Done && frees == 0);
}

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  queuedWakeIsRevoked();
}
