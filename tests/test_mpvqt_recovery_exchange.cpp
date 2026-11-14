#include "RendererOwners.h"
#include "RecoveryExchange.h"
#include <QtTest>

class RecoveryExchangeGenerations : public QObject {
    Q_OBJECT
private slots:
    void oldRetirementCannotRevokeReplacement();
    void replacementRejectsOldFailureAndUsability();
};

void RecoveryExchangeGenerations::oldRetirementCannotRevokeReplacement()
{
    auto core = std::make_shared<RendererRuntime::CoreOwner>();
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
    auto core = std::make_shared<RendererRuntime::CoreOwner>();
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
