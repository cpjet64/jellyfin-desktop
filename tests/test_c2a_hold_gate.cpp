#include <QtTest/QtTest>
#include "c2a_hold_gate.h"

using namespace std::chrono_literals;

class TestC2aHoldGate : public QObject
{
  Q_OBJECT
private slots:
  void readyBeforeArm();
  void observedReleaseBeforeIo();
  void timeoutBeforeEntry();
  void timeoutAfterArm();
  void cancellationPreventsRearm();
};

void TestC2aHoldGate::readyBeforeArm()
{
  std::atomic<bool> held{false}, observed{false};
  C2aHoldGate gate(held, [&] { return observed.load(); }, [](auto) {}, [](auto) {});
  QVERIFY(!gate.arm());
  gate.start(5s);
  QTRY_VERIFY_WITH_TIMEOUT(gate.ready.load(), 1000);
  QVERIFY(gate.arm());
  QVERIFY(held.load());
  gate.cancel();
  gate.join();
  QVERIFY(!held.load());
}

void TestC2aHoldGate::observedReleaseBeforeIo()
{
  std::atomic<bool> held{false}, observed{false}, capturedHeld{false}, ioSawClear{false};
  C2aHoldGate gate(held, [&] { return observed.load(); },
    [&](auto) { capturedHeld = held.load(); }, [&](auto) { ioSawClear = !held.load(); });
  gate.start(5s);
  QTRY_VERIFY_WITH_TIMEOUT(gate.ready.load(), 1000);
  QVERIFY(gate.arm());
  observed = true;
  QTRY_VERIFY_WITH_TIMEOUT(gate.outcome.load() != C2aHoldGate::Outcome::Pending, 1000);
  gate.join();
  QVERIFY(gate.outcome.load() == C2aHoldGate::Outcome::Observed);
  QVERIFY(capturedHeld.load());
  QVERIFY(ioSawClear.load());
  QVERIFY(!held.load());
}

void TestC2aHoldGate::timeoutBeforeEntry()
{
  std::atomic<bool> held{false};
  C2aHoldGate gate(held, [] { return false; }, [](auto) {}, [](auto) {});
  gate.start(20ms);
  QTRY_VERIFY_WITH_TIMEOUT(gate.ready.load(), 1000);
  QTRY_VERIFY_WITH_TIMEOUT(gate.outcome.load() != C2aHoldGate::Outcome::Pending, 1000);
  gate.join();
  QVERIFY(gate.outcome.load() == C2aHoldGate::Outcome::Timeout);
  QVERIFY(!gate.arm());
  QVERIFY(!held.load());
}

void TestC2aHoldGate::cancellationPreventsRearm()
{
  std::atomic<bool> held{false};
  C2aHoldGate gate(held, [] { return false; }, [](auto) {}, [](auto) {});
  gate.start(5s);
  QTRY_VERIFY_WITH_TIMEOUT(gate.ready.load(), 1000);
  QVERIFY(gate.arm());
  gate.cancel();
  gate.join();
  QVERIFY(gate.outcome.load() == C2aHoldGate::Outcome::Cancelled);
  QVERIFY(!held.load());
  QVERIFY(!gate.arm());
}

void TestC2aHoldGate::timeoutAfterArm()
{
  std::atomic<bool> held{false};
  C2aHoldGate gate(held, [] { return false; }, [](auto) {}, [](auto) {});
  gate.start(200ms);
  QTRY_VERIFY_WITH_TIMEOUT(gate.ready.load(), 1000);
  QVERIFY(gate.arm());
  QVERIFY(held.load());
  QTRY_VERIFY_WITH_TIMEOUT(gate.outcome.load() != C2aHoldGate::Outcome::Pending, 1000);
  gate.join();
  QVERIFY(gate.outcome.load() == C2aHoldGate::Outcome::Timeout);
  QVERIFY(!held.load());
  QVERIFY(!gate.arm());
}

QTEST_GUILESS_MAIN(TestC2aHoldGate)
#include "test_c2a_hold_gate.moc"
