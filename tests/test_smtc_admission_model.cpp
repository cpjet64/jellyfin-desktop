#include <QtTest/QtTest>
#include <chrono>
#include <condition_variable>
#include <thread>
#include "../src/taskbar/SmtcAdmission.h"
#include <vector>

class Receiver : public QObject
{
public:
  explicit Receiver(std::vector<int>& values) : values(values) {}
  static void dispatch(QObject* object, int value) { static_cast<Receiver*>(object)->values.push_back(value); }
  std::vector<int>& values;
};

class TestSmtcAdmissionModel : public QObject
{
  Q_OBJECT
private slots:
  void closedQueuedDelivery();
  void heldOldAdmission();
};

void TestSmtcAdmissionModel::closedQueuedDelivery()
{
  std::vector<int> values;
  auto receiver = std::make_unique<Receiver>(values);
  auto state = std::make_shared<SmtcAdmission>(receiver.get());
  QVERIFY(SmtcAdmission::enqueue(state, 7, &Receiver::dispatch));
  state->close();
  state->close();
  QVERIFY(!SmtcAdmission::enqueue(state, 8, &Receiver::dispatch));
  QCoreApplication::processEvents();
  QVERIFY(values.empty());
}

void TestSmtcAdmissionModel::heldOldAdmission()
{
  std::vector<int> oldValues, newValues;
  auto receiver = std::make_unique<Receiver>(oldValues);
  auto oldState = std::make_shared<SmtcAdmission>(receiver.get());
  std::weak_ptr<SmtcAdmission> oldWeak = oldState;
  std::mutex mutex;
  std::condition_variable cv;
  bool entered = false, release = false, accepted = true;
  std::thread worker([&] {
    { std::unique_lock<std::mutex> lock(mutex); entered = true; cv.notify_one();
      cv.wait_for(lock, std::chrono::seconds(5), [&] { return release; }); }
    accepted = SmtcAdmission::enqueue(oldState, 1, &Receiver::dispatch);
  });
  bool observed;
  { std::unique_lock<std::mutex> lock(mutex);
    observed = cv.wait_for(lock, std::chrono::seconds(5), [&] { return entered; }); }
  oldState->close();
  receiver.reset();
  Receiver newReceiver(newValues);
  auto newState = std::make_shared<SmtcAdmission>(&newReceiver);
  { std::lock_guard<std::mutex> lock(mutex); release = true; }
  cv.notify_one();
  worker.join();
  QVERIFY(observed);
  QVERIFY(!accepted);
  QVERIFY(SmtcAdmission::enqueue(newState, 2, &Receiver::dispatch));
  QCoreApplication::processEvents();
  QVERIFY(oldValues.empty());
  QCOMPARE(newValues.size(), size_t(1));
  QCOMPARE(newValues.front(), 2);
  newState->close();
  oldState.reset();
  QVERIFY(oldWeak.expired());
}

QTEST_GUILESS_MAIN(TestSmtcAdmissionModel)
#include "test_smtc_admission_model.moc"
