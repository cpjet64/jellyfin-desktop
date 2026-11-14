#include "RendererOwners.h"
#include "RecoveryExchange.h"
#include <MpvController>
#include <QtTest>

class RecoveryExchangeGenerations : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void oldRetirementCannotRevokeReplacement();
    void replacementRejectsOldFailureAndUsability();
private:
    std::shared_ptr<RendererRuntime::CoreOwner> core;
    QThread *worker = nullptr;
};

void RecoveryExchangeGenerations::init()
{
    const auto reservation = RendererRuntime::reserveCore(this, 1, [](auto, auto, auto) {});
    QCOMPARE(reservation.status, RendererRuntime::AdmissionStatus::Reserved);
    worker = new QThread;
    auto *controller = new MpvController(MpvController::EventMode::External);
    auto *relay = new RendererRuntime::WakeRelay;
    controller->moveToThread(worker);
    relay->moveToThread(worker);
    worker->start();
    QVERIFY(QMetaObject::invokeMethod(controller, &MpvController::init, Qt::BlockingQueuedConnection));
    core = RendererRuntime::CoreOwner::adoptReserved(reservation.token, controller->mpv(), controller, worker,
                                                      relay, QCoreApplication::instance());
    QVERIFY(core);
}

void RecoveryExchangeGenerations::cleanup()
{
    if (!core) return;
    QSignalSpy finished(worker, &QThread::finished);
    QVERIFY(core->requestFinalization());
    bool admitted = false;
    const auto pending = RendererRuntime::reserveCore(this, 2,
        [&admitted](RendererRuntime::AdmissionStatus status, quint64, quint64) {
            admitted = status == RendererRuntime::AdmissionStatus::Reserved;
        });
    if (pending.status == RendererRuntime::AdmissionStatus::Reserved) admitted = true;
    QVERIFY(!finished.isEmpty() || finished.wait(15000));
    QTRY_VERIFY_WITH_TIMEOUT(admitted, 15000);
    RendererRuntime::cancelCoreReservation(pending.token);
    core.reset();
    worker = nullptr;
    RendererRuntime::closeAdmissionDispatcher(this);
}

void RecoveryExchangeGenerations::oldRetirementCannotRevokeReplacement()
{
    auto oldOwner = RendererRuntime::GenerationOwner::create(1, core);
    auto newOwner = RendererRuntime::GenerationOwner::create(2, core);
    QVERIFY(oldOwner && newOwner);
    RecoveryExchange exchange;
    std::lock_guard<std::mutex> lock(exchange.mutex);
    exchange.install(oldOwner, 1);
    exchange.ticketPending = true;
    exchange.install(newOwner, 2);
    exchange.ticketPending = true;
    int revoked = 0;
    QVERIFY(!exchange.retireIfCurrent(oldOwner, 1, [&] { ++revoked; }));
    QCOMPARE(revoked, 0);
    QVERIFY(exchange.ticketPending);
    QVERIFY(exchange.retireIfCurrent(newOwner, 2, [&] { ++revoked; }));
    QCOMPARE(revoked, 1);
    QVERIFY(!exchange.ticketPending);
    QVERIFY(oldOwner->retireOnOriginalContext());
    QVERIFY(newOwner->retireOnOriginalContext());
}

void RecoveryExchangeGenerations::replacementRejectsOldFailureAndUsability()
{
    auto oldOwner = RendererRuntime::GenerationOwner::create(1, core);
    auto newOwner = RendererRuntime::GenerationOwner::create(2, core);
    QVERIFY(oldOwner && newOwner);
    RecoveryExchange exchange;
    std::lock_guard<std::mutex> lock(exchange.mutex);
    exchange.install(oldOwner, 1);
    QVERIFY(exchange.recordFailure(oldOwner, 1));
    exchange.requestGeneration = 1;
    exchange.requestId = 10;
    QVERIFY(exchange.recordUsability(oldOwner, 1, true));
    exchange.ready = true;
    exchange.readyGeneration = 1;
    exchange.install(newOwner, 2);
    QVERIFY(!exchange.ready && !exchange.resultPending && !exchange.contextFailurePending);
    exchange.requestGeneration = 2;
    exchange.requestId = 20;
    QVERIFY(!exchange.recordFailure(oldOwner, 1));
    QVERIFY(!exchange.recordUsability(oldOwner, 1, true));
    QVERIFY(!exchange.owns(oldOwner, 1));
    QCOMPARE(exchange.requestId, quint64(20));
    QVERIFY(!exchange.contextFailurePending && !exchange.resultPending);
    QVERIFY(exchange.recordUsability(newOwner, 2, true));
    QCOMPARE(exchange.resultGeneration, quint64(2));
    QCOMPARE(exchange.resultId, quint64(20));
    QVERIFY(oldOwner->retireOnOriginalContext());
    QVERIFY(newOwner->retireOnOriginalContext());
}

QTEST_GUILESS_MAIN(RecoveryExchangeGenerations)
#include "test_mpvqt_recovery_exchange.moc"
