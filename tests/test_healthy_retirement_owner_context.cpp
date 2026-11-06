#include "healthy_restart/lifetime_qt.h"
#include <QCoreApplication>
#include <QOpenGLContext>
#include <QObject>
#include <cstdio>
#include <cstdlib>
#include <memory>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "check failed at line %d\n", __LINE__); std::abort(); } } while (false)
using namespace HealthyRetirement;

static void contextLifetimeInvalidates(bool signalFirst)
{
  QObject dispatcher;
  int contextValue = 0, frees = 0;
  const Identity identity{11, 22, 33};
  Operations ops{[] { return std::int64_t{1}; }, [=] { return identity; }, [](void*) {},
    [] { return BarrierResult::Ready; }, [](std::uint64_t) { return true; },
    [&](void*) { ++frees; }, [](void*) { return true; }};
  const auto slot = std::make_shared<Slot>(7, 100, [] { return std::int64_t{1}; });
  CHECK(slot->beginCreation() && slot->beforeCreate());
  CHECK(slot->recordCreated(&contextValue));
  const auto owner = slot->install(identity, &contextValue, ops);
  CHECK(owner);
  const auto token = std::make_shared<QtToken>(slot, &dispatcher, [] {});
  auto context = std::make_unique<QOpenGLContext>();
  CHECK(token->watchContext(context.get()));
  if (signalFirst)
    CHECK(QMetaObject::invokeMethod(context.get(), "aboutToBeDestroyed", Qt::DirectConnection));
  else
    context.reset();
  CHECK(!owner->enterRender(7) && !owner->enterCallback(7));
  CHECK(!owner->ack(7) && !owner->mayDestroy(7, true) && frees == 0);
}

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  contextLifetimeInvalidates(true);
  contextLifetimeInvalidates(false);
}
