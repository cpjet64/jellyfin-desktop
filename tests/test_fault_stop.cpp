#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QQuickWindow>
#include <QKeyEvent>
#include <MpvController>
#include <mpv/client.h>
#include "input/InputComponent.h"
#include "input/InputKeyboard.h"
#include "player/PlayerComponent.h"
#include "shared/Paths.h"
#include "system/SystemComponent.h"
#include "system/WebClientFaultGuard.h"
#include "ui/EventFilter.h"

class TestFaultStop : public QObject
{
  Q_OBJECT
private:
  void healthyRoute(InputComponent& input, QSignalSpy& forwarded, QSignalSpy& nativeStops);
  void detachedRoute(InputComponent& input, QSignalSpy& forwarded, QSignalSpy& nativeStops);
  void appleStop(InputComponent& input, QSignalSpy& forwarded, QSignalSpy& nativeStops, bool detached);
  void nativeResult();
  void nativeSeekHealthy();
  void keyboardSequence(InputComponent& input, QSignalSpy& nativeStops);
private slots:
  void routesAndNativeResult();
};

static int hostCalls = 0;
static int failedCommands = 0;
static int seekCommands = 0;
static QStringList seekArguments;
static int failStop(mpv_handle*, const char**)
{
  ++failedCommands;
  return MPV_ERROR_COMMAND;
}

static int failSeek(mpv_handle*, const char** args)
{
  ++seekCommands;
  seekArguments = {QString::fromUtf8(args[0]), QString::fromUtf8(args[1]), QString::fromUtf8(args[2])};
  return MPV_ERROR_COMMAND;
}

void TestFaultStop::healthyRoute(InputComponent& input, QSignalSpy& forwarded, QSignalSpy& nativeStops)
{
  input.registerHostCommand("test-fault-route", []() { ++hostCalls; });
  input.registerHostCommand("fullscreen", []() { ++hostCalls; });
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
  QCOMPARE(hostCalls, 1);
  input.sendAction("host:test-fault-route");
  QCOMPARE(hostCalls, 1);
  input.sendAction("host:player seek 10");
  input.sendAction("host:cycle_setting video.aspect");
  QCOMPARE(unavailableActions.count(), 5);
  input.sendAction("host:fullscreen");
  QCOMPARE(hostCalls, 2);
  QSignalSpy seeks(&input, &InputComponent::positionSeek);
  input.seekTo(1234);
  auto* worker = QThread::create([&input]() { input.seekTo(1234); });
  worker->start();
  QVERIFY(worker->wait(5000));
  delete worker;
  QCoreApplication::processEvents();
  QCOMPARE(seeks.count(), 0);
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
  QCOMPARE(player.requestNativeSeek(1234).status, PlayerComponent::NativeSeekResult::Status::Unavailable);
  const auto result = PlayerComponent::checkedStop(controller->mpv(), failStop);
  QCOMPARE(result.status, PlayerComponent::NativeStopResult::Status::Error);
  QCOMPARE(result.error, MPV_ERROR_COMMAND);
  QCOMPARE(failedCommands, 1);
  mpv_set_wakeup_callback(controller->mpv(), nullptr, nullptr);
  QCoreApplication::processEvents();
#ifdef MPVQT_EXTERNAL_EVENTS
  player.releaseMpvController(controller);
#endif
  mpv_terminate_destroy(controller->mpv());
}

void TestFaultStop::nativeSeekHealthy()
{
  QObject owner;
  auto* controller = new MpvController(&owner);
  controller->init();
  PlayerComponent player;
  player.setMpvController(controller);
  const char* args[] = {"seek", "1.234", "absolute+exact", nullptr};
  const int actualError = mpv_command(controller->mpv(), args);
  const auto actual = player.requestNativeSeek(1234);
  QCOMPARE(actual.error, actualError);
  QCOMPARE(actual.status, actualError < 0 ? PlayerComponent::NativeSeekResult::Status::Error
                                        : PlayerComponent::NativeSeekResult::Status::Accepted);
  const auto failed = PlayerComponent::checkedSeek(controller->mpv(), 1234, failSeek);
  QCOMPARE(failed.status, PlayerComponent::NativeSeekResult::Status::Error);
  QCOMPARE(failed.error, MPV_ERROR_COMMAND);
  QCOMPARE(seekCommands, 1);
  QCOMPARE(seekArguments, QStringList({"seek", "1.234", "absolute+exact"}));
  PlayerComponent::NativeSeekResult offThread{PlayerComponent::NativeSeekResult::Status::Accepted, 0};
  auto* worker = QThread::create([&]() { offThread = player.requestNativeSeek(1234); });
  worker->start();
  QVERIFY(worker->wait(5000));
  delete worker;
  QCOMPARE(offThread.status, PlayerComponent::NativeSeekResult::Status::Unavailable);
  mpv_set_wakeup_callback(controller->mpv(), nullptr, nullptr);
  QCoreApplication::processEvents();
#ifdef MPVQT_EXTERNAL_EVENTS
  player.releaseMpvController(controller);
#endif
  mpv_terminate_destroy(controller->mpv());
}

void TestFaultStop::keyboardSequence(InputComponent& input, QSignalSpy& nativeStops)
{
  QQuickWindow window;
  window.setProperty("webDesktopMode", true);
  EventFilter filter(&window);
  window.installEventFilter(&filter);
  auto connection = connect(&InputKeyboard::Get(), &InputBase::receivedInput, this,
    [&input](const QString& source, const QString& key, InputBase::InputkeyState state) {
      input.remapInput(source, key, state);
    });
  const int baseline = nativeStops.count();
  QKeyEvent overrideEvent(QEvent::ShortcutOverride, Qt::Key_Back, Qt::NoModifier);
  QKeyEvent press(QEvent::KeyPress, Qt::Key_Back, Qt::NoModifier);
  QKeyEvent repeatRelease(QEvent::KeyRelease, Qt::Key_Back, Qt::NoModifier, QString(), true);
  QKeyEvent repeatPress(QEvent::KeyPress, Qt::Key_Back, Qt::NoModifier, QString(), true);
  QKeyEvent release(QEvent::KeyRelease, Qt::Key_Back, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &overrideEvent);
  QCoreApplication::sendEvent(&window, &press);
  QCoreApplication::sendEvent(&window, &repeatRelease);
  QCoreApplication::sendEvent(&window, &repeatPress);
  QCOMPARE(nativeStops.count(), baseline + 1);
  QCoreApplication::sendEvent(&window, &release);
  QCoreApplication::sendEvent(&window, &press);
  QCOMPARE(nativeStops.count(), baseline + 2);
  QCoreApplication::sendEvent(&window, &release);
#ifdef Q_OS_WIN
  QSignalSpy keyboard(&InputKeyboard::Get(), &InputBase::receivedInput);
  QKeyEvent media(QEvent::KeyPress, Qt::Key_MediaStop, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &media);
  QCOMPARE(keyboard.count(), 0);
#endif
  disconnect(connection);
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
  QFile keyboard(profile.path() + "/profiles/inputmaps/keyboard-test.json");
  QVERIFY(keyboard.open(QIODevice::WriteOnly));
  QVERIFY(keyboard.write(R"({"name":"Keyboard Generic","idmatcher":"Keyboard.*","mapping":{"Back":"stop"}})") > 0);
  keyboard.close();
  auto& input = InputComponent::Get();
  QVERIFY(input.m_mappings->loadMappings());
  QVERIFY(input.m_mappings->mapToAction("LIRC", "KEY_STOP").contains(QVariant(QStringLiteral("stop"))));
  QVERIFY(input.m_mappings->mapToAction("AppleMediaKeys", "KEY_STOP").contains(QVariant(QStringLiteral("stop"))));
  QVERIFY(input.m_mappings->mapToAction("Keyboard", "Back").contains(QVariant(QStringLiteral("stop"))));
  QSignalSpy forwarded(&input, &InputComponent::hostInput);
  QSignalSpy nativeStops(&input, &InputComponent::nativeStopResult);
  healthyRoute(input, forwarded, nativeStops);
  appleStop(input, forwarded, nativeStops, false);
  nativeSeekHealthy();
  detachedRoute(input, forwarded, nativeStops);
  appleStop(input, forwarded, nativeStops, true);
  keyboardSequence(input, nativeStops);
  nativeResult();
}

QTEST_MAIN(TestFaultStop)
#include "test_fault_stop.moc"
