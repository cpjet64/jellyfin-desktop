#include <QtTest/QtTest>
#include <chrono>
#include <condition_variable>
#include <thread>
#include "smtc_admission_model.h"

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
  auto receiver = std::make_unique<SmtcModelReceiver>(values);
  auto state = std::make_shared<SmtcAdmissionModel>(receiver.get());
  QVERIFY(smtcModelEnqueue(state, 7));
  smtcModelClose(state);
  smtcModelClose(state);
  QVERIFY(!smtcModelEnqueue(state, 8));
  QCoreApplication::processEvents();
  QVERIFY(values.empty());
}

void TestSmtcAdmissionModel::heldOldAdmission()
{
  std::vector<int> oldValues, newValues;
  auto receiver = std::make_unique<SmtcModelReceiver>(oldValues);
  auto oldState = std::make_shared<SmtcAdmissionModel>(receiver.get());
  std::weak_ptr<SmtcAdmissionModel> oldWeak = oldState;
  std::mutex mutex;
  std::condition_variable cv;
  bool entered = false, release = false, accepted = true;
  std::thread worker([&] {
    { std::unique_lock<std::mutex> lock(mutex); entered = true; cv.notify_one();
      cv.wait_for(lock, std::chrono::seconds(5), [&] { return release; }); }
    accepted = smtcModelEnqueue(oldState, 1);
  });
  bool observed;
  { std::unique_lock<std::mutex> lock(mutex);
    observed = cv.wait_for(lock, std::chrono::seconds(5), [&] { return entered; }); }
  smtcModelClose(oldState);
  receiver.reset();
  SmtcModelReceiver newReceiver(newValues);
  auto newState = std::make_shared<SmtcAdmissionModel>(&newReceiver);
  { std::lock_guard<std::mutex> lock(mutex); release = true; }
  cv.notify_one();
  worker.join();
  QVERIFY(observed);
  QVERIFY(!accepted);
  QVERIFY(smtcModelEnqueue(newState, 2));
  QCoreApplication::processEvents();
  QVERIFY(oldValues.empty());
  QCOMPARE(newValues.size(), size_t(1));
  QCOMPARE(newValues.front(), 2);
  smtcModelClose(newState);
  oldState.reset();
  QVERIFY(oldWeak.expired());
}

QTEST_GUILESS_MAIN(TestSmtcAdmissionModel)
#include "test_smtc_admission_model.moc"
