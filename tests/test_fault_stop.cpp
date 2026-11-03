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
  QSignalSpy forwarded(&input, &InputComponent::hostInput);
  QSignalSpy nativeStops(&input, &InputComponent::nativeStopResult);
  QSignalSpy unavailableActions(&input, &InputComponent::hostActionUnavailable);
  static int hostCalls = 0;
  input.registerHostCommand("test-fault-route", []() { ++hostCalls; });
  SystemComponent::Get().hello("test");
  input.sendAction("stop");
  QCOMPARE(forwarded.count(), 1);
  input.executeActions({"host:test-fault-route"});
  QCOMPARE(hostCalls, 1);
  QCOMPARE(forwarded.count(), 1);

  QObject controllerOwner;
  auto* controller = new MpvController(&controllerOwner);
  controller->init();
  PlayerComponent player;
  player.setMpvController(controller);
  QCOMPARE(player.requestNativeStop().status, PlayerComponent::NativeStopResult::Status::Accepted);
  auto* worker = QThread::create([&input]() { input.sendAction("stop"); });
  worker->start();
  QVERIFY(worker->wait(5000));
  delete worker;
  QTRY_COMPARE(forwarded.count(), 2);
  input.remapInput("direct", "stop", InputBase::KeyDown);
  input.remapInput("direct", "stop", InputBase::KeyDown);
  input.remapInput("direct", "stop", InputBase::KeyUp);
  QCOMPARE(forwarded.count(), 4);
  QCOMPARE(nativeStops.count(), 0);

  input.m_currentLongPressAction.insert("short", "stop");
  input.m_autoRepeatActions = {"stop"};
  input.m_autoRepeatTimer->start(1000);
  WebClientFaultGuard::instance().observeRendererLoss(0, 0);
  QVERIFY(!input.m_autoRepeatTimer->isActive());
  QVERIFY(input.m_currentLongPressAction.isEmpty());
  QVERIFY(input.m_autoRepeatActions.isEmpty());
  input.sendAction("stop");
  input.sendAction("pause");
  QCOMPARE(nativeStops.count(), 1);
  QCOMPARE(nativeStops.at(0).at(0).toInt(), static_cast<int>(PlayerComponent::NativeStopResult::Status::Unavailable));
  QCOMPARE(unavailableActions.count(), 1);
  input.remapInput("direct", "stop", InputBase::KeyDown);
  input.remapInput("direct", "stop", InputBase::KeyDown);
  QCOMPARE(nativeStops.count(), 2);
  input.remapInput("direct", "stop", InputBase::KeyUp);
  input.remapInput("direct", "stop", InputBase::KeyDown);
  QCOMPARE(nativeStops.count(), 3);
  input.remapInput("direct", "stop", InputBase::KeyUp);
  input.executeActions({"host:test-fault-route"});
  QCOMPARE(forwarded.count(), 4);
  QCOMPARE(hostCalls, 2);
  input.sendAction("host:test-fault-route");
  QCOMPARE(hostCalls, 3);

  mpv_set_wakeup_callback(controller->mpv(), nullptr, nullptr);
  QCoreApplication::processEvents();
  const char* quit[] = {"quit", nullptr};
  QVERIFY(mpv_command(controller->mpv(), quit) >= 0);
  int idle = 0;
  QTRY_VERIFY_WITH_TIMEOUT(mpv_get_property(controller->mpv(), "idle-active", MPV_FORMAT_FLAG, &idle) < 0, 5000);
  const auto result = player.requestNativeStop();
  QCOMPARE(result.status, PlayerComponent::NativeStopResult::Status::Error);
  QVERIFY(result.error < 0);
  mpv_terminate_destroy(controller->mpv());
}

QTEST_MAIN(TestFaultStop)
#include "test_fault_stop.moc"
