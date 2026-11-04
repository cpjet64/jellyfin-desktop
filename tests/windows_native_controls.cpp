// Exercises the production callback directly; this does not simulate OS dispatch.
#include <QtTest/QtTest>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QUrl>
#include <MpvController>
#include <mpv/client.h>
#include <wrl/implements.h>
#include <memory>

#include "input/InputComponent.h"
#include "player/MpvVideoItem.h"
#include "player/PlayerComponent.h"
#include "settings/SettingsComponent.h"
#include "system/SystemComponent.h"
#include "system/WebClientFaultGuard.h"
#include "taskbar/TaskbarComponentWin.h"
#include "mpv_test_bootstrap.h"
#include "mpv_test_profile.h"
#include "render_startup_diagnostics.h"

using namespace ABI::Windows::Media;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::WinRt;

class ButtonArgs final : public RuntimeClass<RuntimeClassFlags<WinRt>, ISystemMediaTransportControlsButtonPressedEventArgs>
{
  InspectableClass(L"Jellyfin.Test.ButtonArgs", BaseTrust)
public:
  SystemMediaTransportControlsButton button{};
  IFACEMETHODIMP get_Button(SystemMediaTransportControlsButton* value) override
  {
    if (!value) return E_POINTER;
    *value = button;
    return S_OK;
  }
};

class TestWindowsNativeControls : public QObject
{
  Q_OBJECT
private:
  void press(TaskbarComponentWin& taskbar, SystemMediaTransportControlsButton button);
  void healthy(TaskbarComponentWin& taskbar, InputComponent& input);
  void detached(TaskbarComponentWin& taskbar, InputComponent& input, PlayerComponent& player, mpv_handle* mpv);
private slots:
  void callbackPath();
};

void TestWindowsNativeControls::press(TaskbarComponentWin& taskbar, SystemMediaTransportControlsButton button)
{
  auto args = Microsoft::WRL::Make<ButtonArgs>();
  QVERIFY(args);
  args->button = button;
  QCOMPARE(taskbar.buttonPressed(nullptr, args.Get()), S_OK);
}

void TestWindowsNativeControls::healthy(TaskbarComponentWin& taskbar, InputComponent& input)
{
  SystemComponent::Get().hello("test");
  QSignalSpy actions(&input, &InputComponent::hostInput);
  const QList<QPair<SystemMediaTransportControlsButton, QString>> cases{
    {SystemMediaTransportControlsButton_Play, "play_pause"},
    {SystemMediaTransportControlsButton_Pause, "play_pause"},
    {SystemMediaTransportControlsButton_Next, "next"},
    {SystemMediaTransportControlsButton_Previous, "previous"},
    {SystemMediaTransportControlsButton_Stop, "stop"},
    {SystemMediaTransportControlsButton_FastForward, "seek_forward"},
    {SystemMediaTransportControlsButton_Rewind, "seek_backward"},
    {SystemMediaTransportControlsButton_ChannelUp, "channelup"},
    {SystemMediaTransportControlsButton_ChannelDown, "channeldown"}};
  for (const auto& item : cases) {
    press(taskbar, item.first);
    QVERIFY(!QTest::currentTestFailed());
    QCOMPARE(actions.count(), 1);
    QCOMPARE(actions.takeFirst().at(0).toStringList(), QStringList{item.second});
  }
  press(taskbar, SystemMediaTransportControlsButton_Record);
  QVERIFY(!QTest::currentTestFailed());
  QCOMPARE(actions.count(), 0);
}

void TestWindowsNativeControls::detached(TaskbarComponentWin& taskbar, InputComponent& input,
                                         PlayerComponent& player, mpv_handle* mpv)
{
  std::unique_ptr<mpv_handle, decltype(&mpv_destroy)> observer(
    mpv_create_client(mpv, "native-controls-observer"), &mpv_destroy);
  QVERIFY(observer);
  QCOMPARE(mpv_request_event(observer.get(), MPV_EVENT_SEEK, 1), 0);
  QCOMPARE(mpv_request_event(observer.get(), MPV_EVENT_START_FILE, 1), 0);
  int pause = 0;
  QCOMPARE(mpv_get_property(mpv, "pause", MPV_FORMAT_FLAG, &pause), 0);
  QCOMPARE(pause, 0);
  WebClientFaultGuard::instance().observeRendererLoss(0, 17);
  QSignalSpy unavailable(&input, &InputComponent::hostActionUnavailable);
  QSignalSpy stops(&input, &InputComponent::nativeStopResult);
  const QList<SystemMediaTransportControlsButton> buttons{
    SystemMediaTransportControlsButton_Play, SystemMediaTransportControlsButton_Pause,
    SystemMediaTransportControlsButton_Next, SystemMediaTransportControlsButton_Previous,
    SystemMediaTransportControlsButton_FastForward, SystemMediaTransportControlsButton_Rewind,
    SystemMediaTransportControlsButton_ChannelUp, SystemMediaTransportControlsButton_ChannelDown};
  const qint64 position = player.getPosition();
  for (auto button : buttons) {
    press(taskbar, button);
    QVERIFY(!QTest::currentTestFailed());
  }
  press(taskbar, SystemMediaTransportControlsButton_Record);
  QVERIFY(!QTest::currentTestFailed());
  QCOMPARE(unavailable.count(), buttons.size());
  QCOMPARE(stops.count(), 0);
  QTest::qWait(500);
  QVERIFY(player.getPosition() > position + 200);
  QCOMPARE(mpv_get_property(mpv, "pause", MPV_FORMAT_FLAG, &pause), 0);
  QCOMPARE(pause, 0);
  for (mpv_event* event = mpv_wait_event(observer.get(), 0); event->event_id != MPV_EVENT_NONE;
       event = mpv_wait_event(observer.get(), 0)) {
    QVERIFY(event->event_id != MPV_EVENT_SEEK);
    QVERIFY(event->event_id != MPV_EVENT_START_FILE);
  }
  press(taskbar, SystemMediaTransportControlsButton_Stop);
  QVERIFY(!QTest::currentTestFailed());
  QCOMPARE(stops.count(), 1);
  QCOMPARE(stops.at(0).at(0).toInt(), static_cast<int>(PlayerComponent::NativeStopResult::Status::Accepted));
  int idle = 0;
  QTRY_VERIFY_WITH_TIMEOUT(mpv_get_property(mpv, "idle-active", MPV_FORMAT_FLAG, &idle) >= 0 && idle, 10000);
}

void TestWindowsNativeControls::callbackPath()
{
  const QString evidence = qEnvironmentVariable("RECOVERY_EVIDENCE_DIR");
  const QString media = qEnvironmentVariable("RECOVERY_MEDIA");
  const QString root = qEnvironmentVariable("JELLYFIN_TEST_OWNED_ROOT", evidence);
  QVERIFY(QFileInfo(evidence).isDir());
  QVERIFY(QFileInfo(media).isFile());
  const QString rootPath = QFileInfo(root).canonicalFilePath();
  const QString evidencePath = QFileInfo(evidence).canonicalFilePath();
  QVERIFY(!rootPath.isEmpty() && !evidencePath.isEmpty());
  const QString relative = QDir(rootPath).relativeFilePath(evidencePath);
  QVERIFY(relative != ".." && !relative.startsWith("../") && !QDir::isAbsolutePath(relative));
  Q_INIT_RESOURCE(resources);
  QString error;
  QVERIFY2(MpvTestBootstrap::prepareMpvTestEnvironment(root, &error), qPrintable(error));
  QVERIFY2(prepareMpvTestProfile(evidence, &error), qPrintable(error));
  QVERIFY(SettingsComponent::Get().componentInitialize());
  QQuickWindow window;
  window.resize(640, 360);
  auto* video = new MpvVideoItem(window.contentItem());
  video->setParent(window.contentItem());
  video->setSize(QSizeF(640, 360));
  RenderStartupDiagnostics diagnostics;
  diagnostics.observe(window, *video);
  auto& player = PlayerComponent::Get();
  QVERIFY(player.componentInitialize());
  player.setWindow(&window);
  window.show();
  QTRY_VERIFY_WITH_TIMEOUT(diagnostics.ready() || diagnostics.failed(), 15000);
  QVERIFY(!diagnostics.failed());
  QVERIFY(video->controller());
  QTRY_COMPARE_WITH_TIMEOUT(player.requestNativeStop().status, PlayerComponent::NativeStopResult::Status::Accepted, 15000);
  QVERIFY(player.load(QUrl::fromLocalFile(media).toString(), {{"autoplay", true}}, {{"type", "video"}}));
  QTRY_VERIFY_WITH_TIMEOUT(player.getPosition() > 500, 10000);
  TaskbarComponentWin taskbar;
  taskbar.m_initialized = false; // No OS registration was made by this fixture.
  auto& input = InputComponent::Get();
  healthy(taskbar, input);
  QVERIFY(!QTest::currentTestFailed());
  detached(taskbar, input, player, video->controller()->mpv());
  QVERIFY(!QTest::currentTestFailed());
}

int main(int argc, char** argv)
{
  QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  QApplication app(argc, argv);
  TestWindowsNativeControls test;
  return QTest::qExec(&test, argc, argv);
}

#include "windows_native_controls.moc"
