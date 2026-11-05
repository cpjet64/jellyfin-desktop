#include "system/RecoveryBridgeSession.h"
#include "system/WebClientFaultGuard.h"
#include <QWebChannel>
#include <QThread>
#include <QtTest/QtTest>

class TestRecoveryBridgeHost : public QObject
{
  Q_OBJECT
private slots:
  void attachNavigateAndRendererLoss();
};

static QVariantMap handshake(RecoveryBridgeEndpoint* endpoint, const QString& command)
{
  return {{"version", 1}, {"sessionId", endpoint->sessionId()},
          {"commandId", command}, {"operation", "handshake"}};
}

void TestRecoveryBridgeHost::attachNavigateAndRendererLoss()
{
  auto& guard = WebClientFaultGuard::instance();
  QObject firstWindow, secondWindow, observedWindow, movedWindow;
  QWebChannel firstChannel, secondChannel, observedChannel, movedChannel, replacementChannel;
  firstWindow.setProperty("recoveryNavigationObserved", false);
  secondWindow.setProperty("recoveryNavigationObserved", false);
  observedWindow.setProperty("recoveryNavigationObserved", true);
  movedWindow.setProperty("recoveryNavigationObserved", false);
  auto* worker = QThread::create([&] { guard.attachRecoveryBridge(&firstWindow, &firstChannel); });
  worker->start();
  if (!worker->wait(3000)) qFatal("host test thread did not settle");
  delete worker;
  QVERIFY(!firstChannel.registeredObjects().contains("recoveryBridge"));
  guard.attachRecoveryBridge(&firstWindow, &firstChannel);
  guard.attachRecoveryBridge(&secondWindow, &secondChannel);
  guard.attachRecoveryBridge(&observedWindow, &observedChannel);
  guard.attachRecoveryBridge(&movedWindow, &movedChannel);
  auto* first = qobject_cast<RecoveryBridgeEndpoint*>(firstChannel.registeredObjects().value("recoveryBridge"));
  auto* second = qobject_cast<RecoveryBridgeEndpoint*>(secondChannel.registeredObjects().value("recoveryBridge"));
  auto* moved = qobject_cast<RecoveryBridgeEndpoint*>(movedChannel.registeredObjects().value("recoveryBridge"));
  QVERIFY(first && second && moved && first != second);
  QVERIFY(!observedChannel.registeredObjects().contains("recoveryBridge"));
  QCOMPARE(first->invoke(handshake(first, "1")).value("status").toString(), "accepted");
  QCOMPARE(second->invoke(handshake(second, "1")).value("status").toString(), "accepted");
  guard.attachRecoveryBridge(&firstWindow, &firstChannel);
  QCOMPARE(first->invoke(handshake(first, "2")).value("status").toString(), "accepted");
  guard.attachRecoveryBridge(&movedWindow, &replacementChannel);
  QCOMPARE(moved->invoke(handshake(moved, "1")).value("status").toString(), "revoked");
  QVERIFY(!replacementChannel.registeredObjects().contains("recoveryBridge"));
  guard.observeRecoveryNavigation(&firstWindow);
  QCOMPARE(first->invoke(handshake(first, "3")).value("status").toString(), "revoked");
  guard.attachRecoveryBridge(&firstWindow, &firstChannel);
  QCOMPARE(firstChannel.registeredObjects().value("recoveryBridge"), static_cast<QObject*>(first));
  QCOMPARE(second->invoke(handshake(second, "2")).value("status").toString(), "accepted");
  guard.observeRendererLoss(0, 0);
  QCOMPARE(second->invoke(handshake(second, "3")).value("status").toString(), "revoked");
  guard.attachRecoveryBridge(&observedWindow, &observedChannel);
  QVERIFY(!observedChannel.registeredObjects().contains("recoveryBridge"));
}

QTEST_GUILESS_MAIN(TestRecoveryBridgeHost)
#include "test_recovery_bridge_host.moc"
