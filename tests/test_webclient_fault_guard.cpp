#include <QtTest/QtTest>
#include "system/SystemComponent.h"
#include "system/WebClientFaultGuard.h"

class TestWebClientFaultGuard : public QObject
{
  Q_OBJECT
private slots:
  void rendererLossIsOneWay();
};

void TestWebClientFaultGuard::rendererLossIsOneWay()
{
  auto& system = SystemComponent::Get();
  auto& guard = WebClientFaultGuard::instance();
  QSignalSpy changed(&guard, &WebClientFaultGuard::rendererLostChanged);
  QVERIFY(!guard.rendererLost());
  QVERIFY(!system.isWebClientConnected());
  system.hello("first");
  QVERIFY(system.isWebClientConnected());
  guard.observeRendererLoss(0, 17);
  QVERIFY(guard.rendererLost());
  QCOMPARE(guard.terminationStatus(), 0);
  QCOMPARE(guard.terminationExitCode(), 17);
  QVERIFY(!system.isWebClientConnected());
  QCOMPARE(changed.count(), 1);
  guard.observeRendererLoss(2, 99);
  QCOMPARE(guard.terminationStatus(), 0);
  QCOMPARE(guard.terminationExitCode(), 17);
  system.hello("late");
  QVERIFY(!system.isWebClientConnected());
  QCOMPARE(changed.count(), 1);
}

QTEST_MAIN(TestWebClientFaultGuard)
#include "test_webclient_fault_guard.moc"
