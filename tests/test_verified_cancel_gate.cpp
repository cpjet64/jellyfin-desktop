#include "fixture_restart/RestartChild.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <cstdio>
#include <cstdlib>
using namespace FixtureRestart;
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "check failed at line %d\n", __LINE__); std::abort(); } } while (false)

namespace {
Grant grant() { return {1, 2, 3, QByteArray(64, 'a'), 3000, true}; }
QByteArray digest() { return QCryptographicHash::hash(encodeGrant(grant()), QCryptographicHash::Sha256); }
RestartChild::Actions actions()
{
  RestartChild::Actions value;
  value.now = [] { return qint64{10}; };
  value.armActual = [] { return RestartChild::ArmResult{RestartChild::ArmResult::Status::Armed, {7, 8, 9, 10}}; };
  value.beginCleanup = [](quint64, quint64) { return true; };
  value.pollCleanup = [] { return RestartChild::CleanupResult{RestartChild::CleanupResult::Status::Pending, {}, CancelFailure::None}; };
  return value;
}
bool yes(void*) { return true; }

void validBeforeGrant()
{
  int grantChannel, controlChannel; ChildGate gate(&grantChannel, 100);
  RestartChild child(gate, &grantChannel, &controlChannel, grant(), 9, digest(), actions());
  CHECK(!gate.matchesVerifiedCancel(1, 4));
  QByteArray response;
  CHECK(child.control(&controlChannel, encodeControl({ControlKind::UserStop, 1, 4}), response));
  CHECK(gate.canceled() && gate.matchesVerifiedCancel(1, 4));
  CHECK(!gate.matchesVerifiedCancel(0, 4) && !gate.matchesVerifiedCancel(1, 0));
  CHECK(!gate.matchesVerifiedCancel(2, 4) && !gate.matchesVerifiedCancel(1, 5));
  CHECK(decodeCancelNoArm(response, {1, 9, 4, 1, digest()}));
  CHECK(!child.grant(&grantChannel, encodeGrant(grant()), 10, true, response));
}

void validAfterGrantAndDuplicate()
{
  int grantChannel, controlChannel; ChildGate gate(&grantChannel, 100);
  auto callbacks = actions(); bool visibleDuringCleanup = false;
  callbacks.beginCleanup = [&](quint64 sequence, quint64 revision) {
    visibleDuringCleanup = gate.matchesVerifiedCancel(sequence, revision) &&
        !gate.matchesVerifiedCancel(sequence + 1, revision);
    return true;
  };
  RestartChild child(gate, &grantChannel, &controlChannel, grant(), 9, digest(), callbacks);
  QByteArray response;
  CHECK(child.grant(&grantChannel, encodeGrant(grant()), 10, true, response));
  CHECK(child.control(&controlChannel, encodeControl({ControlKind::NewPlay, 2, 4}), response));
  CHECK(gate.matchesVerifiedCancel(2, 4) && visibleDuringCleanup);
  CHECK(!child.control(&controlChannel, encodeControl({ControlKind::NewPlay, 2, 4}), response));
  CHECK(!child.control(&controlChannel, encodeControl({ControlKind::UserStop, 3, 5}), response));
  CHECK(!gate.matchesVerifiedCancel(3, 5) && gate.matchesVerifiedCancel(2, 4));
}

void invalidControlsNeverVerify()
{
  for (int kind = 0; kind != 4; ++kind) {
    int grantChannel, controlChannel, wrongChannel; ChildGate gate(&grantChannel, 100);
    RestartChild child(gate, &grantChannel, &controlChannel, grant(), 9, digest(), actions());
    QByteArray response("unchanged");
    QByteArray frame = encodeControl({ControlKind::UserStop, 1, 4});
    if (kind == 0) frame = "malformed";
    if (kind == 1) frame = encodeControl({ControlKind::UserStop, 0, 4});
    if (kind == 2) frame = encodeControl({ControlKind::UserStop, 1, 3});
    const void* channel = kind == 3 ? &wrongChannel : &controlChannel;
    CHECK(!child.control(channel, frame, response));
    CHECK(response == "unchanged" && !gate.matchesVerifiedCancel(1, 4));
    CHECK(!gate.matchesVerifiedCancel(0, 4) && !gate.matchesVerifiedCancel(1, 3));
  }
}

void internalRevocationCannotForgeReceipt()
{
  int grantChannel, controlChannel; ChildGate gate(&grantChannel, 100);
  RestartChild child(gate, &grantChannel, &controlChannel, grant(), 9, digest(), actions());
  CHECK(child.revoke() && gate.canceled() && !gate.matchesVerifiedCancel(1, 4));
  QByteArray response;
  CHECK(!child.control(&controlChannel, encodeControl({ControlKind::UserStop, 1, 4}), response));
  CHECK(!gate.matchesVerifiedCancel(1, 4));
  ChildGate direct(&grantChannel, 100);
  CHECK(direct.cancel(yes, nullptr) && !direct.matchesVerifiedCancel(1, 4));
}
}
int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  validBeforeGrant(); validAfterGrantAndDuplicate(); invalidControlsNeverVerify(); internalRevocationCannotForgeReceipt();
}
