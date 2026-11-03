#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <MpvController>
#include <mpv/client.h>
#include "input/InputComponent.h"
#include "player/PlayerComponent.h"
#include "shared/Paths.h"
#include "system/SystemComponent.h"
#include "system/WebClientFaultGuard.h"

class TestFaultStop : public QObject
{
  Q_OBJECT
private:
  void healthyRoute(InputComponent& input, QSignalSpy& forwarded, QSignalSpy& nativeStops);
  void detachedRoute(InputComponent& input, QSignalSpy& forwarded, QSignalSpy& nativeStops);
  void appleStop(InputComponent& input, QSignalSpy& forwarded, QSignalSpy& nativeStops, bool detached);
  void nativeResult();
private slots:
  void routesAndNativeResult();
};

static int hostCalls = 0;
static int failedCommands = 0;
static int failStop(mpv_handle*, const char**)
{
  ++failedCommands;
  return MPV_ERROR_COMMAND;
}

void TestFaultStop::healthyRoute(InputComponent& input, QSignalSpy& forwarded, QSignalSpy& nativeStops)
{
  input.registerHostCommand("test-fault-route", []() { ++hostCalls; });
  SystemComponent::Get().hello("test");
  input.sendAction("stop");
  QCOMPARE(forwarded.count(), 1);
  input.executeActions({"host:test-fault-route"});
  QCOMPARE(hostCalls, 1);
  QCOMPARE(forwarded.count(), 1);
  auto* worker = QThread::create([&input]() { input.sendAction("stop"); });
  worker->start();
  QVERIFY(worker->wait(5000));
  delete worker;
  QTRY_COMPARE(forwarded.count(), 2);
  input.remapInput("LIRC", "KEY_STOP", InputBase::KeyDown);
  input.remapInput("LIRC", "KEY_STOP", InputBase::KeyDown);
  input.remapInput("LIRC", "KEY_STOP_LIRCUP", InputBase::KeyUp);
  QCOMPARE(forwarded.count(), 4);
  QCOMPARE(nativeStops.count(), 0);
  input.remapInput("LIRC", "KEY_STOP", InputBase::KeyDown);
  QCOMPARE(forwarded.count(), 5);
}

void TestFaultStop::detachedRoute(InputComponent& input, QSignalSpy& forwarded, QSignalSpy& nativeStops)
{
  const int forwardedBaseline = forwarded.count();
  QSignalSpy unavailableActions(&input, &InputComponent::hostActionUnavailable);
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
  input.remapInput("LIRC", "KEY_STOP", InputBase::KeyDown);
  input.remapInput("LIRC", "KEY_STOP", InputBase::KeyDown);
  QCOMPARE(nativeStops.count(), 1);
  input.remapInput("LIRC", "KEY_STOP_LIRCUP", InputBase::KeyUp);
  input.remapInput("LIRC", "KEY_STOP", InputBase::KeyDown);
  QCOMPARE(nativeStops.count(), 2);
  input.remapInput("LIRC", "KEY_STOP_LIRCUP", InputBase::KeyUp);
  input.remapInput("LIRC", "KEY_STOP", InputBase::KeyDown);
  QCOMPARE(nativeStops.count(), 3);
  input.remapInput("LIRC", "KEY_STOP_LIRCUP", InputBase::KeyUp);
  input.executeActions({"host:test-fault-route"});
  QCOMPARE(forwarded.count(), forwardedBaseline);
  QCOMPARE(hostCalls, 2);
  input.sendAction("host:test-fault-route");
  QCOMPARE(hostCalls, 3);
}

void TestFaultStop::appleStop(InputComponent& input, QSignalSpy& forwarded, QSignalSpy& nativeStops, bool detached)
{
  const int forwardedBaseline = forwarded.count();
  const int nativeBaseline = nativeStops.count();
  input.remapInput("AppleMediaKeys", "KEY_STOP", InputBase::KeyPressed);
  QCOMPARE(forwarded.count(), forwardedBaseline + (detached ? 0 : 1));
  QCOMPARE(nativeStops.count(), nativeBaseline + (detached ? 1 : 0));
}

void TestFaultStop::nativeResult()
{
  PlayerComponent unavailable;
  QCOMPARE(unavailable.requestNativeStop().status, PlayerComponent::NativeStopResult::Status::Unavailable);
  QObject controllerOwner;
  auto* controller = new MpvController(&controllerOwner);
  controller->init();
  PlayerComponent player;
  player.setMpvController(controller);
  QCOMPARE(player.requestNativeStop().status, PlayerComponent::NativeStopResult::Status::Accepted);
  const auto result = PlayerComponent::checkedStop(controller->mpv(), failStop);
  QCOMPARE(result.status, PlayerComponent::NativeStopResult::Status::Error);
  QCOMPARE(result.error, MPV_ERROR_COMMAND);
  QCOMPARE(failedCommands, 1);
  mpv_set_wakeup_callback(controller->mpv(), nullptr, nullptr);
  QCoreApplication::processEvents();
  mpv_terminate_destroy(controller->mpv());
}

void TestFaultStop::routesAndNativeResult()
{
  QTemporaryDir profile(QDir::currentPath() + "/fault-stop-profile-XXXXXX");
  QVERIFY(profile.isValid());
  Paths::setConfigDir(profile.path());
  QVERIFY(QDir().mkpath(profile.path() + "/profiles/inputmaps"));
  const QString mapping = QFINDTESTDATA("../resources/inputmaps/LIRC.json");
  QVERIFY(!mapping.isEmpty());
  QVERIFY(QFile::copy(mapping, profile.path() + "/profiles/inputmaps/LIRC.json"));
  const QString appleMapping = QFINDTESTDATA("../resources/inputmaps/apple-media-keys.json");
  QVERIFY(!appleMapping.isEmpty());
  QVERIFY(QFile::copy(appleMapping, profile.path() + "/profiles/inputmaps/apple-media-keys.json"));
  auto& input = InputComponent::Get();
  QVERIFY(input.m_mappings->loadMappings());
  QVERIFY(input.m_mappings->mapToAction("LIRC", "KEY_STOP").contains(QVariant(QStringLiteral("stop"))));
  QVERIFY(input.m_mappings->mapToAction("AppleMediaKeys", "KEY_STOP").contains(QVariant(QStringLiteral("stop"))));
  QSignalSpy forwarded(&input, &InputComponent::hostInput);
  QSignalSpy nativeStops(&input, &InputComponent::nativeStopResult);
  healthyRoute(input, forwarded, nativeStops);
  appleStop(input, forwarded, nativeStops, false);
  detachedRoute(input, forwarded, nativeStops);
  appleStop(input, forwarded, nativeStops, true);
  nativeResult();
}

QTEST_MAIN(TestFaultStop)
#include "test_fault_stop.moc"
