#include <QtTest/QtTest>
#include <QElapsedTimer>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "c2/RenderRetirementState.h"

using State = RenderRetirementState;
using namespace std::chrono_literals;

class TestC2RetirementState : public QObject
{
  Q_OBJECT
private slots:
  void immediateCallbackAndFailure();
  void staleAndBlockedRetirement();
  void waitsForInflightCallbacks();
  void missingJobDoesNotAcknowledge();
};

void TestC2RetirementState::immediateCallbackAndFailure()
{
  State state(41);
  // Registration can invoke the callback before registration returns.
  auto registerCallback = [&state] {
    const bool admitted = state.enterCallback();
    state.leaveCallback();
    return admitted;
  };
  QVERIFY(registerCallback());
  QVERIFY(state.accepts(41));
  QVERIFY(!state.accepts(40));
  QVERIFY(state.failOnce(State::Stage::Create, -7));
  QVERIFY(state.failurePending(41));
  QVERIFY(!state.failOnce(State::Stage::Render, -8));
  QVERIFY(!state.accepts(41));
  const bool admittedAfterFailure = state.enterCallback();
  state.leaveCallback();
  QVERIFY(!admittedAfterFailure);
  QVERIFY(state.request(41));
  QCOMPARE(state.retire(41, true, [] {}, [] {}), State::Result::Retired);
  QVERIFY(!state.failurePending(41));
}

void TestC2RetirementState::staleAndBlockedRetirement()
{
  State state(42);
  int unregisters = 0;
  int frees = 0;
  bool admittedDuringUnregister = true;
  const auto unregister = [&] {
    ++unregisters;
    admittedDuringUnregister = state.enterCallback();
    state.leaveCallback();
  };
  const auto free = [&] { ++frees; };
  QVERIFY(!state.request(41));
  QVERIFY(state.accepts(42));
  QCOMPARE(state.retire(41, true, unregister, free), State::Result::Stale);
  QVERIFY(state.request(42));
  QVERIFY(!state.accepts(42));
  QCOMPARE(state.retire(42, false, unregister, free), State::Result::BlockedNoContext);
  QCOMPARE(unregisters, 0);
  QCOMPARE(frees, 0);
  // A blocked attempt is not an acknowledgement; explicit retry can retire.
  QCOMPARE(state.retire(42, true, unregister, free), State::Result::Retired);
  QCOMPARE(unregisters, 1);
  QCOMPARE(frees, 1);
  QVERIFY(!admittedDuringUnregister);
  QCOMPARE(state.retire(42, true, unregister, free), State::Result::Stale);
  QVERIFY(!state.request(42));
  QCOMPARE(unregisters, 1);
  QCOMPARE(frees, 1);
}

void TestC2RetirementState::missingJobDoesNotAcknowledge()
{
  State state(44);
  std::atomic<int> acknowledged{0};
  QVERIFY(state.request(44));
  {
    // A discarded render job owns no completion side effect.
    auto job = [&] { ++acknowledged; };
    Q_UNUSED(job);
  }
  QTest::qWait(50);
  QCOMPARE(acknowledged.load(), 0);
  QVERIFY(!state.accepts(44));
}

void TestC2RetirementState::waitsForInflightCallbacks()
{
  State state(43);
  std::mutex mutex;
  std::condition_variable changed;
  int entered = 0;
  bool release = false;
  std::atomic<int> admitted{0}, unregistered{0}, freed{0}, acknowledged{0};
  auto callback = [&] {
    if (state.enterCallback()) ++admitted;
    {
      std::unique_lock<std::mutex> lock(mutex);
      ++entered;
      changed.notify_all();
      changed.wait_for(lock, 5s, [&] { return release; });
    }
    state.leaveCallback();
  };
  std::thread first(callback), second(callback);
  bool bothEntered;
  {
    std::unique_lock<std::mutex> lock(mutex);
    bothEntered = changed.wait_for(lock, 5s, [&] { return entered == 2; });
  }
  const bool requested = state.request(43);
  std::thread retire([&] {
    const auto result = state.retire(43, true, [&] { ++unregistered; }, [&] { ++freed; });
    if (result == State::Result::Retired && freed.load() == 1) ++acknowledged;
  });
  QElapsedTimer deadline;
  deadline.start();
  while (!unregistered.load() && deadline.elapsed() < 5000) std::this_thread::yield();
  const bool unregisteredBeforeRelease = unregistered.load() == 1;
  const bool stillBlocked = freed.load() == 0 && acknowledged.load() == 0;
  const bool rejectedStaleWork = !state.accepts(42) && !state.accepts(43);
  {
    std::lock_guard<std::mutex> lock(mutex);
    release = true;
  }
  changed.notify_all();
  first.join();
  second.join();
  retire.join();
  QVERIFY(bothEntered);
  QVERIFY(requested);
  QCOMPARE(admitted.load(), 2);
  QVERIFY(unregisteredBeforeRelease);
  QVERIFY(stillBlocked);
  QVERIFY(rejectedStaleWork);
  QCOMPARE(unregistered.load(), 1);
  QCOMPARE(freed.load(), 1);
  QCOMPARE(acknowledged.load(), 1);
}

QTEST_GUILESS_MAIN(TestC2RetirementState)
#include "test_c2_retirement_state.moc"
