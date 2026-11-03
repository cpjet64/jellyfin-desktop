#include <QtTest/QtTest>
#include <QThread>
#include <MpvController>
#include <mpv/client.h>
#include "input/InputComponent.h"
#include "player/PlayerComponent.h"
#include "system/SystemComponent.h"
#include "system/WebClientFaultGuard.h"

class TestFaultStop : public QObject
{
  Q_OBJECT
private slots:
  void routesAndNativeResult();
};

void TestFaultStop::routesAndNativeResult()
{
  PlayerComponent unavailable;
  QCOMPARE(unavailable.requestNativeStop().status, PlayerComponent::NativeStopResult::Status::Unavailable);
  auto& input = InputComponent::Get();
  auto& player = PlayerComponent::Get();
  QSignalSpy forwarded(&input, &InputComponent::hostInput);
  int hostCalls = 0;
  input.registerHostCommand("test-fault-route", [&hostCalls]() { ++hostCalls; });
  SystemComponent::Get().hello("test");
  input.sendAction("stop");
  QCOMPARE(forwarded.count(), 1);
  input.executeActions({"host:test-fault-route"});
  QCOMPARE(hostCalls, 1);
  QCOMPARE(forwarded.count(), 1);

  MpvController controller;
  controller.init();
  player.setMpvController(&controller);
  QCOMPARE(player.requestNativeStop().status, PlayerComponent::NativeStopResult::Status::Accepted);
  auto* worker = QThread::create([&input]() { input.sendAction("stop"); });
  worker->start();
  QVERIFY(worker->wait(5000));
  delete worker;
  QTRY_COMPARE(forwarded.count(), 2);

  WebClientFaultGuard::instance().observeRendererLoss(0, 0);
  input.sendAction("stop");
  input.sendAction("pause");
  input.executeActions({"host:test-fault-route"});
  QCOMPARE(forwarded.count(), 2);
  QCOMPARE(hostCalls, 2);
  input.sendAction("host:test-fault-route");
  QCOMPARE(hostCalls, 3);

  mpv_set_wakeup_callback(controller.mpv(), nullptr, nullptr);
  QCoreApplication::processEvents();
  const char* quit[] = {"quit", nullptr};
  QVERIFY(mpv_command(controller.mpv(), quit) >= 0);
  int idle = 0;
  QTRY_VERIFY_WITH_TIMEOUT(mpv_get_property(controller.mpv(), "idle-active", MPV_FORMAT_FLAG, &idle) < 0, 5000);
  const auto result = player.requestNativeStop();
  QCOMPARE(result.status, PlayerComponent::NativeStopResult::Status::Error);
  QVERIFY(result.error < 0);
  mpv_terminate_destroy(controller.mpv());
}

QTEST_MAIN(TestFaultStop)
#include "test_fault_stop.moc"
