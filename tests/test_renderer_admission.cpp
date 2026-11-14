#include "RendererOwners.h"
#include <MpvController>
#include <QCoreApplication>
#include <QPointer>
#include <QSignalSpy>
#include <QThread>
#include <QtTest>
#include <memory>

using namespace RendererRuntime;

class RendererAdmissionTest : public QObject {
  Q_OBJECT
private slots:
  void init();
  void cleanup();
  void delayedFinalizationReservesOnce();
  void cancelledWaiterIsNotCalled();
  void completionBeforeRegistrationAllowsReservation();
  void quarantineWinsAdmission();
  void failedReservationRetiresBeforeReadmission();
  void internalWakeBeforeBindingIsDrained();
private:
  bool adopt(quint64 token, bool internal = false);
  std::shared_ptr<CoreOwner> owner_;
  QPointer<QThread> worker_;
  QPointer<MpvController> controller_;
  bool quarantined_ = false;
};

void RendererAdmissionTest::init()
{
  const auto reservation = reserveCore(this, 1, [](auto, auto, auto) {});
  QCOMPARE(reservation.status, AdmissionStatus::Reserved);
  if (QByteArray(QTest::currentTestFunction()) == "failedReservationRetiresBeforeReadmission") {
    auto* worker = new QThread;
    auto* controller = new MpvController(MpvController::EventMode::External);
    auto* relay = new WakeRelay;
    // Requires the nonfatal initialization patch's safe pre-init accessor.
    QVERIFY(controller->mpv() == nullptr);
    worker_ = worker;
    controller->moveToThread(worker);
    relay->moveToThread(worker);
    worker->start();
    QVERIFY(!CoreOwner::adoptReserved(reservation.token, nullptr, controller, worker, relay,
                                      QCoreApplication::instance()));
    owner_ = CoreOwner::adoptFailedReservation(reservation.token, controller, worker, relay,
                                                QCoreApplication::instance());
    QVERIFY(owner_);
    return;
  }
  const bool internal = QByteArray(QTest::currentTestFunction()) == "internalWakeBeforeBindingIsDrained";
  QVERIFY(adopt(reservation.token, internal));
}

bool RendererAdmissionTest::adopt(quint64 token, bool internal)
{
  auto* worker = new QThread;
  auto* controller = new MpvController(internal ? MpvController::EventMode::Internal : MpvController::EventMode::External);
  auto* relay = new WakeRelay;
  worker_ = worker;
  controller_ = controller;
  controller->moveToThread(worker);
  relay->moveToThread(worker);
  worker->start();
  if (!QMetaObject::invokeMethod(controller, &MpvController::init, Qt::BlockingQueuedConnection)) return false;
  owner_ = CoreOwner::adoptReserved(token, controller->mpv(), controller, worker, relay,
                                    QCoreApplication::instance());
  return bool(owner_);
}

void RendererAdmissionTest::cleanup()
{
  if (!owner_) return;
  if (!quarantined_) {
    owner_->requestFinalization();
    QTRY_VERIFY_WITH_TIMEOUT(owner_->nativeDestroyed.load(), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(worker_.isNull(), 15000);
  }
  closeAdmissionDispatcher(this);
  owner_.reset();
}

void RendererAdmissionTest::delayedFinalizationReservesOnce()
{
  auto lease = owner_->acquireNative();
  QVERIFY(lease);
  QVERIFY(owner_->requestFinalization());
  int calls = 0; quint64 granted = 0;
  const auto pending = reserveCore(this, 42, [&](AdmissionStatus status, quint64 token, quint64 revision) {
    if (status == AdmissionStatus::Reserved && revision == 42) { ++calls; granted = token; }
  });
  QCOMPARE(pending.status, AdmissionStatus::Deferred);
  QCoreApplication::processEvents();
  QCOMPARE(calls, 0);
  lease = {};
  QTRY_COMPARE_WITH_TIMEOUT(calls, 1, 15000);
  QVERIFY(granted == pending.token);
  QVERIFY(owner_->nativeDestroyed.load());
  QVERIFY(worker_.isNull() || !worker_->isRunning());
  QCoreApplication::processEvents();
  QCOMPARE(calls, 1);
  auto old = owner_;
  QVERIFY(adopt(granted));
  QVERIFY(owner_ != old && old->nativeDestroyed.load());
  QCOMPARE(calls, 1);
}

void RendererAdmissionTest::cancelledWaiterIsNotCalled()
{
  auto lease = owner_->acquireNative();
  QVERIFY(lease);
  QVERIFY(owner_->requestFinalization());
  auto* receiver = new QObject;
  int calls = 0;
  const auto pending = reserveCore(receiver, 9, [&](auto, auto, auto) { ++calls; });
  QCOMPARE(pending.status, AdmissionStatus::Deferred);
  cancelCoreReservation(pending.token);
  closeAdmissionDispatcher(receiver);
  delete receiver;
  lease = {};
  QTRY_VERIFY_WITH_TIMEOUT(owner_->nativeDestroyed.load(), 15000);
  QTRY_VERIFY_WITH_TIMEOUT(worker_.isNull(), 15000);
  QCoreApplication::processEvents();
  QCOMPARE(calls, 0);
}

void RendererAdmissionTest::completionBeforeRegistrationAllowsReservation()
{
  // Retain the old shared_ptr even after native teardown and worker completion.
  auto old = owner_;
  QVERIFY(owner_->requestFinalization());
  QTRY_VERIFY_WITH_TIMEOUT(owner_->nativeDestroyed.load(), 15000);
  QTRY_VERIFY_WITH_TIMEOUT(worker_.isNull(), 15000);
  const auto next = reserveCore(this, 7, [](auto, auto, auto) {});
  QCOMPARE(next.status, AdmissionStatus::Reserved);
  QVERIFY(old && old->nativeDestroyed.load());
  cancelCoreReservation(next.token);
}

void RendererAdmissionTest::quarantineWinsAdmission()
{
  auto lease = owner_->acquireNative();
  QVERIFY(lease);
  lease.contain();
  quarantined_ = true;
  lease = {};
  const auto next = reserveCore(this, 8, [](auto, auto, auto) {});
  QCOMPARE(next.status, AdmissionStatus::Quarantined);
  QVERIFY(owner_->faultQuarantined.load());
}

void RendererAdmissionTest::failedReservationRetiresBeforeReadmission()
{
  QVERIFY(owner_->failedInitialization);
  QVERIFY(!owner_->acquireNative());
  auto old = owner_;
  int calls = 0; quint64 granted = 0;
  const auto pending = reserveCore(this, 12, [&](AdmissionStatus status, quint64 token, quint64 revision) {
    if (status == AdmissionStatus::Reserved && revision == 12) { ++calls; granted = token; }
  });
  QCOMPARE(pending.status, AdmissionStatus::Deferred);
  QCoreApplication::processEvents();
  QCOMPARE(calls, 0);
  QVERIFY(owner_->requestFinalization());
  QTRY_COMPARE_WITH_TIMEOUT(calls, 1, 15000);
  QVERIFY(granted == pending.token);
  QVERIFY(old->nativeDestroyed.load());
  QVERIFY(worker_.isNull() || !worker_->isRunning());
  cancelCoreReservation(granted);
}

void RendererAdmissionTest::internalWakeBeforeBindingIsDrained()
{
  QVERIFY(controller_);
  QSignalSpy changed(controller_, &MpvController::propertyChanged);
  QVERIFY(changed.isValid());
  auto lease = owner_->acquireNative();
  QVERIFY(lease);
  // The pinned client synchronously registers a pending initial observation.
  QCOMPARE(mpv_observe_property(lease.get(), 70, "pause", MPV_FORMAT_FLAG), 0);
  // Initialization queues a real native wake. Unregister before binding so no
  // later wake can rescue the test; the worker fence drains any queued wake.
  owner_->closeExternalWakeup();
  QVERIFY(QMetaObject::invokeMethod(controller_, &MpvController::eventHandler, Qt::BlockingQueuedConnection));
  QCOMPARE(changed.size(), 0); // Before binding, the queued handler has no lease.
  auto core = owner_;
  QVERIFY(QMetaObject::invokeMethod(controller_, [controller = controller_, core] {
    if (controller) controller->bindRecoveryCore(core);
  }, Qt::BlockingQueuedConnection));
  QTRY_VERIFY_WITH_TIMEOUT(!changed.isEmpty(), 15000);
  bool sawPause = false;
  for (const auto& args : changed) if (args.at(0).toString() == QStringLiteral("pause")) sawPause = true;
  QVERIFY(sawPause);
  lease = {};
}

QTEST_GUILESS_MAIN(RendererAdmissionTest)
#include "test_renderer_admission.moc"
