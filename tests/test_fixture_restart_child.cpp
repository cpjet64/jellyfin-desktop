#include "fixture_restart/RestartChild.h"
#include <QCoreApplication>
#include <cstdlib>
#define CHECK(value) do { if (!(value)) std::abort(); } while (false)

using namespace FixtureRestart;
static bool yes(void*) { return true; }
static Grant binding() { return {1, 2, 3, QByteArray(64, 'a'), 3000, true}; }

static void cancelBeforeGrant()
{
  int grantToken, controlToken, stops = 0, loads = 0;
  ChildGate gate(&grantToken, 100);
  RestartChild child(gate, &grantToken, &controlToken, binding(), {
    [] { return true; }, [&] { ++stops; return gate.cancel(yes, nullptr); }, [&] { ++loads; return true; }, [] { return true; }});
  QByteArray ack;
  CHECK(child.control(&controlToken, encodeControl({ControlKind::UserStop, 1, 4}), ack));
  Control decoded;
  CHECK(decodeControl(ack, decoded) && decoded.kind == ControlKind::Ack && decoded.sequence == 1 && decoded.revision == 4);
  CHECK(!child.grant(&grantToken, encodeGrant(binding()), 10, true));
  CHECK(!child.load() && stops == 1 && loads == 0);
}

static void cancelAfterLoad()
{
  int grantToken, controlToken, stops = 0, loads = 0, seeks = 0;
  ChildGate gate(&grantToken, 100);
  RestartChild child(gate, &grantToken, &controlToken, binding(), {
    [] { return true; }, [&] { ++stops; return gate.cancel(yes, nullptr); },
    [&] { ++loads; return true; }, [&] { ++seeks; return true; }});
  CHECK(child.grant(&grantToken, encodeGrant(binding()), 10, true));
  CHECK(child.load());
  QByteArray ack("unchanged");
  CHECK(!child.control(&grantToken, encodeControl({ControlKind::UserStop, 1, 4}), ack));
  CHECK(ack == "unchanged");
  CHECK(child.control(&controlToken, encodeControl({ControlKind::NewPlay, 2, 4}), ack));
  CHECK(!child.seek() && !child.load() && stops == 1 && loads == 1 && seeks == 0);
  CHECK(!child.control(&controlToken, encodeControl({ControlKind::NewPlay, 2, 4}), ack));
}

static void failedStopAndReentry()
{
  int grantToken, controlToken, loads = 0;
  ChildGate gate(&grantToken, 100);
  RestartChild child(gate, &grantToken, &controlToken, binding(), {
    [] { return true; }, [] { return false; }, [&] { ++loads; return true; }, [] { return true; }});
  CHECK(child.grant(&grantToken, encodeGrant(binding()), 10, true));
  QByteArray ack("unchanged");
  CHECK(!child.control(&controlToken, encodeControl({ControlKind::UserStop, 1, 4}), ack));
  CHECK(ack == "unchanged" && !child.load() && loads == 0);
  ChildGate gate2(&grantToken, 100);
  RestartChild* owner = nullptr;
  QByteArray reentrantAck;
  RestartChild reentrant(gate2, &grantToken, &controlToken, binding(), {
    [] { return true; }, [&] { return gate2.cancel(yes, nullptr); },
    [&] { CHECK(owner->control(&controlToken, encodeControl({ControlKind::UserStop, 1, 4}), reentrantAck)); return true; },
    [] { return true; }});
  owner = &reentrant;
  CHECK(reentrant.grant(&grantToken, encodeGrant(binding()), 10, true));
  CHECK(!reentrant.load() && !reentrant.seek());
  CHECK(!reentrantAck.isEmpty());
}

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  cancelBeforeGrant(); cancelAfterLoad(); failedStopAndReentry();
  return 0;
}
