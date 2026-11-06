#include <fixture_restart/RestartChild.h>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <cstdlib>
using namespace FixtureRestart;
#define CHECK(x) do { if (!(x)) std::abort(); } while (false)

static Grant grant() { return {1, 2, 3, QByteArray(64, 'a'), 3000, true}; }
static QByteArray digest() { return QCryptographicHash::hash(encodeGrant(grant()), QCryptographicHash::Sha256); }
static RestartChild::ArmIdentity identity() { return {10, 11, 12, 13}; }

static void beforeGrant()
{
  int g, c, arms = 0, loads = 0; ChildGate gate(&g, 100);
  RestartChild::Actions a;
  a.now = [] { return 10; }; a.armActual = [&] { ++arms; return RestartChild::ArmResult{}; };
  a.load = [&] { ++loads; return true; };
  RestartChild child(gate, &g, &c, 1, 9, digest(), a); QByteArray result;
  CHECK(child.control(&c, encodeControl({ControlKind::UserStop, 1, 4}), result));
  CHECK(decodeCancelNoArm(result, {1, 9, 4, 1, digest()}));
  CHECK(!child.grant(&g, encodeGrant(grant()), 10, true, result));
  CHECK(!child.load() && arms == 0 && loads == 0);
  CHECK(!child.pollCancellation(result));
}

#ifdef HEALTHY_CHILD_RESTART_TESTING
static void cancelAfterValidationBeforeArm()
{
  int g, c, arms = 0; ChildGate gate(&g, 100);
  RestartChild* owner = nullptr; QByteArray during;
  RestartChild::Actions a; a.now = [] { return 10; };
  a.armActual = [&] { ++arms; return RestartChild::ArmResult{RestartChild::ArmResult::Status::Armed, identity()}; };
  a.beforeArm = [&] {
    CHECK(owner->control(&c, encodeControl({ControlKind::UserStop, 1, 4}), during));
    CHECK(during.isEmpty());
  };
  RestartChild child(gate, &g, &c, 1, 9, digest(), a); owner = &child;
  QByteArray response;
  CHECK(child.grant(&g, encodeGrant(grant()), 10, true, response));
  CHECK(decodeCancelNoArm(response, {1, 9, 4, 1, digest()}));
  CHECK(arms == 0 && !child.load() && !child.seek() && !child.pollCancellation(response));
}
#endif

static void reentrantArm()
{
  int g, c, begins = 0, polls = 0, loads = 0; ChildGate gate(&g, 100);
  QByteArray during, accepted; RestartChild* owner = nullptr;
  RestartChild::Actions a; a.now = [] { return 10; };
  a.armActual = [&] {
    CHECK(owner->control(&c, encodeControl({ControlKind::UserStop, 1, 4}), during));
    CHECK(during.isEmpty());
    return RestartChild::ArmResult{RestartChild::ArmResult::Status::Armed, identity()};
  };
  a.beginCleanup = [&](quint64 seq, quint64 rev) { ++begins; return seq == 1 && rev == 4; };
  a.pollCleanup = [&] { ++polls; return RestartChild::CleanupResult{RestartChild::CleanupResult::Status::Stopped, identity(), CancelFailure::None}; };
  a.load = [&] { ++loads; return true; };
  RestartChild child(gate, &g, &c, 1, 9, digest(), a); owner = &child;
  CHECK(child.grant(&g, encodeGrant(grant()), 10, true, accepted));
  GrantAccepted decoded;
  CHECK(decodeGrantAccepted(accepted, {1, 9, 2, 3, digest()}, decoded));
  CHECK(decoded.host == 10 && decoded.core == 11 && decoded.intentRevision == 12 && decoded.attempt == 13);
  CHECK(!child.load() && loads == 0);
  QByteArray terminal; CHECK(child.pollCancellation(terminal));
  CancelResult parsed;
  CHECK(decodeCancelResult(terminal, {1, 9, 2, 3, 1, 10, 11, 12, 13}, parsed));
  CHECK(parsed.outcome == CancelOutcome::Stopped && begins == 1 && polls == 1);
  CHECK(!child.pollCancellation(terminal));
}

static void canceledDuringUnsuccessfulArm()
{
  for (int unknown = 0; unknown < 2; ++unknown) {
    int g, c, calls = 0; ChildGate gate(&g, 100); RestartChild* owner = nullptr;
    QByteArray during, response; RestartChild::Actions a; a.now = [] { return 10; };
    a.armActual = [&] {
      ++calls; CHECK(owner->control(&c, encodeControl({ControlKind::UserStop, 1, 4}), during));
      CHECK(during.isEmpty());
      return RestartChild::ArmResult{unknown ? RestartChild::ArmResult::Status::Unknown :
          RestartChild::ArmResult::Status::NotArmed, {}};
    };
    RestartChild child(gate, &g, &c, 1, 9, digest(), a); owner = &child;
    const bool result = child.grant(&g, encodeGrant(grant()), 10, true, response);
    CHECK(calls == 1 && !child.load() && !child.seek());
    if (unknown) CHECK(!result && response.isEmpty());
    else CHECK(result && decodeCancelNoArm(response, {1, 9, 4, 1, digest()}));
  }
}

static void rejectsWrongCleanupIdentity()
{
  int g, c, polls = 0; ChildGate gate(&g, 100);
  RestartChild::Actions a; a.now = [] { return 10; };
  a.armActual = [] { return RestartChild::ArmResult{RestartChild::ArmResult::Status::Armed, identity()}; };
  a.beginCleanup = [](quint64, quint64) { return true; };
  a.pollCleanup = [&] {
    if (!polls++) return RestartChild::CleanupResult{RestartChild::CleanupResult::Status::Pending, {}, CancelFailure::None};
    auto wrong = identity(); ++wrong.core;
    return RestartChild::CleanupResult{RestartChild::CleanupResult::Status::Stopped, wrong, CancelFailure::None};
  };
  RestartChild child(gate, &g, &c, 1, 9, digest(), a); QByteArray response;
  CHECK(child.grant(&g, encodeGrant(grant()), 10, true, response));
  response.clear(); CHECK(child.control(&c, encodeControl({ControlKind::NewPlay, 1, 4}), response));
  CHECK(response.isEmpty()); CHECK(!child.pollCancellation(response)); CHECK(response.isEmpty());
}

static void cancellationAfterAcceptance()
{
  int g, c, calls = 0; ChildGate gate(&g, 100);
  RestartChild::Actions a; a.now = [] { return 10; };
  a.armActual = [] { return RestartChild::ArmResult{RestartChild::ArmResult::Status::Armed, identity()}; };
  a.beginCleanup = [&](quint64, quint64) { ++calls; return true; };
  a.pollCleanup = [] { return RestartChild::CleanupResult{RestartChild::CleanupResult::Status::NoLoad, identity(), CancelFailure::None}; };
  RestartChild child(gate, &g, &c, 1, 9, digest(), a); QByteArray response;
  CHECK(child.grant(&g, encodeGrant(grant()), 10, true, response));
  response.clear(); CHECK(child.control(&c, encodeControl({ControlKind::UserStop, 1, 4}), response));
  CancelResult result; CHECK(decodeCancelResult(response, {1, 9, 2, 3, 1, 10, 11, 12, 13}, result));
  CHECK(result.outcome == CancelOutcome::NoLoad && calls == 1 && !child.load());
  CHECK(!child.control(&c, encodeControl({ControlKind::UserStop, 1, 4}), response));
}

static void unknownArmCannotBecomeNoArm()
{
  int g, c; ChildGate gate(&g, 100); RestartChild::Actions a;
  a.now = [] { return 10; };
  a.armActual = [] { return RestartChild::ArmResult{RestartChild::ArmResult::Status::Unknown, {}}; };
  RestartChild child(gate, &g, &c, 1, 9, digest(), a); QByteArray response;
  CHECK(!child.grant(&g, encodeGrant(grant()), 10, true, response));
  CHECK(!child.control(&c, encodeControl({ControlKind::UserStop, 1, 4}), response));
  CHECK(response.isEmpty() && !child.load());
}

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  beforeGrant(); reentrantArm(); canceledDuringUnsuccessfulArm(); rejectsWrongCleanupIdentity();
  cancellationAfterAcceptance(); unknownArmCannotBecomeNoArm();
#ifdef HEALTHY_CHILD_RESTART_TESTING
  cancelAfterValidationBeforeArm();
#endif
}
