#include <fixture_restart/RestartChild.h>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <cstdio>
#include <cstdlib>
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "check failed at line %d\n", __LINE__); std::abort(); } } while (false)

using namespace FixtureRestart;
static bool yes(void*) { return true; }
static Grant binding() { return {1, 2, 3, QByteArray(64, 'a'), 3000, true}; }
static QByteArray digest() { return QCryptographicHash::hash(encodeGrant(binding()), QCryptographicHash::Sha256); }
static QByteArray permit() { return encodeRestorePermit({1, 9, 2, 3, 1, digest()}); }

static void cancelBeforeGrant()
{
  int grantToken, controlToken, stops = 0, loads = 0;
  ChildGate gate(&grantToken, 100);
  RestartChild child(gate, &grantToken, &controlToken, 1, 9, digest(), {
    [] { return true; }, [&] { ++stops; return gate.cancel(yes, nullptr); }, [&] { ++loads; return true; }, [] { return true; }, [] { return 10; }});
  QByteArray ack;
  CHECK(child.control(&controlToken, encodeControl({ControlKind::UserStop, 1, 4}), ack));
  Control decoded;
  CHECK(decodeControl(ack, decoded) && decoded.kind == ControlKind::Ack && decoded.sequence == 1 && decoded.revision == 4);
  CHECK(!child.grant(&grantToken, encodeGrant(binding()), 10, true, ack));
  CHECK(!child.load() && stops == 1 && loads == 0);
}

static void cancelAfterLoad()
{
  int grantToken, controlToken, stops = 0, loads = 0, seeks = 0;
  ChildGate gate(&grantToken, 100);
  RestartChild child(gate, &grantToken, &controlToken, 1, 9, digest(), {
    [] { return true; }, [&] { ++stops; return gate.cancel(yes, nullptr); },
    [&] { ++loads; return true; }, [&] { ++seeks; return true; }, [] { return 10; }});
  QByteArray grantAck; CHECK(child.grant(&grantToken, encodeGrant(binding()), 10, true, grantAck));
  GrantAccepted accepted;
  CHECK(decodeGrantAccepted(grantAck, {1, 9, 2, 3, digest()}, accepted));
  CHECK(!child.load() && loads == 0);
  QByteArray permitReply; CHECK(child.control(&controlToken, permit(), permitReply));
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
  RestartChild child(gate, &grantToken, &controlToken, 1, 9, digest(), {
    [] { return true; }, [] { return false; }, [&] { ++loads; return true; }, [] { return true; }, [] { return 10; }});
  QByteArray grantAck; CHECK(child.grant(&grantToken, encodeGrant(binding()), 10, true, grantAck));
  QByteArray ack("unchanged");
  CHECK(!child.control(&controlToken, encodeControl({ControlKind::UserStop, 1, 4}), ack));
  CHECK(ack == "unchanged" && !child.load() && loads == 0);
  ChildGate gate2(&grantToken, 100);
  RestartChild* owner = nullptr;
  QByteArray reentrantAck;
  RestartChild reentrant(gate2, &grantToken, &controlToken, 1, 9, digest(), {
    [] { return true; }, [&] { return gate2.cancel(yes, nullptr); },
    [&] { CHECK(owner->control(&controlToken, encodeControl({ControlKind::UserStop, 2, 4}), reentrantAck)); return true; },
    [] { return true; }, [] { return 10; }});
  owner = &reentrant;
  CHECK(reentrant.grant(&grantToken, encodeGrant(binding()), 10, true, grantAck));
  CHECK(reentrant.control(&controlToken, permit(), reentrantAck));
  CHECK(!reentrant.load() && !reentrant.seek());
  CHECK(!reentrantAck.isEmpty());
}

static void rejectsBadPermits()
{
  for (int which = 0; which < 5; ++which) {
    int grantToken, controlToken, loads = 0;
    ChildGate gate(&grantToken, 100);
    RestartChild child(gate, &grantToken, &controlToken, 1, 9, digest(), {
      [] { return true; }, [&] { return gate.cancel(yes, nullptr); },
      [&] { ++loads; return true; }, [] { return true; }, [] { return 10; }});
    QByteArray response;
    CHECK(child.grant(&grantToken, encodeGrant(binding()), 10, true, response));
    RestorePermit value{1, 9, 2, 3, 1, digest()};
    if (which == 0) ++value.run;
    if (which == 1) ++value.launch;
    if (which == 2) ++value.grant;
    if (which == 3) ++value.revision;
    if (which == 4) value.digest[0] ^= 1;
    CHECK(!child.control(&controlToken, encodeRestorePermit(value), response));
    CHECK(!child.load() && !child.seek() && loads == 0);
  }
}

static void duplicatePermitRevokes()
{
  int grantToken, controlToken, loads = 0, seeks = 0, stops = 0;
  ChildGate gate(&grantToken, 100);
  RestartChild child(gate, &grantToken, &controlToken, 1, 9, digest(), {
    [] { return true; }, [&] { ++stops; return gate.cancel(yes, nullptr); },
    [&] { ++loads; return true; }, [&] { ++seeks; return true; }, [] { return 10; }});
  QByteArray response; CHECK(child.grant(&grantToken, encodeGrant(binding()), 10, true, response));
  CHECK(child.control(&controlToken, permit(), response) && child.load() && child.seek());
  CHECK(!child.control(&controlToken, permit(), response));
  CHECK(!child.seek() && loads == 1 && seeks == 1 && stops == 1);
}

static void committedGrantMutations()
{
  for (int which = 0; which < 3; ++which) {
    int grantToken, controlToken, loads = 0;
    ChildGate gate(&grantToken, 100);
    RestartChild child(gate, &grantToken, &controlToken, 1, 9, digest(), {
      [] { return true; }, [&] { return gate.cancel(yes, nullptr); },
      [&] { ++loads; return true; }, [] { return true; }, [] { return 10; }});
    auto changed = binding();
    if (which == 0) ++changed.run;
    if (which == 1) ++changed.positionMs;
    if (which == 2) changed.desiredPaused = !changed.desiredPaused;
    QByteArray response("unchanged");
    CHECK(!child.grant(&grantToken, encodeGrant(changed), 10, true, response));
    CHECK(response == "unchanged" && !child.load() && !child.seek() && loads == 0);
  }
}

static void permitExpiresBeforeDispatch()
{
  int grantToken, controlToken, loads = 0; qint64 clock = 10;
  ChildGate gate(&grantToken, 100);
  RestartChild child(gate, &grantToken, &controlToken, 1, 9, digest(), {
    [] { return true; }, [&] { return gate.cancel(yes, nullptr); },
    [&] { ++loads; return true; }, [] { return true; }, [&] { return clock; }});
  QByteArray response; CHECK(child.grant(&grantToken, encodeGrant(binding()), 10, true, response));
  CHECK(child.control(&controlToken, permit(), response));
  clock = 100;
  CHECK(!child.load() && !child.seek() && loads == 0);
}

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  cancelBeforeGrant(); cancelAfterLoad(); failedStopAndReentry(); rejectsBadPermits();
  duplicatePermitRevokes(); committedGrantMutations(); permitExpiresBeforeDispatch();
  return 0;
}
