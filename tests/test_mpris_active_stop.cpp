// A private D-Bus client exercises MPRIS against a real offscreen mpv core.
#include <QtTest/QtTest>
#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTimer>
#include <QUrl>
#include <MpvController>
#include <mpv/client.h>

#include "core/ProfileManager.h"
#include "input/InputComponent.h"
#include "mpris/MprisComponent.h"
#include "player/PlayerComponent.h"
#include "settings/SettingsComponent.h"
#include "system/SystemComponent.h"
#include "system/WebClientFaultGuard.h"
#include "mpv_test_bootstrap.h"
#include "mpv_test_profile.h"
#include "mpris_offscreen_scene.h"

namespace {
constexpr auto objectPath = "/org/mpris/MediaPlayer2";
constexpr auto playerInterface = "org.mpris.MediaPlayer2.Player";

QDBusMessage invoke(const QDBusConnection& client, const QString& service,
                    const QString& interface, const QString& method, const QVariantList& args = {})
{
  auto message = QDBusMessage::createMethodCall(service, objectPath, interface, method);
  message.setArguments(args);
  QDBusPendingCallWatcher watcher(client.asyncCall(message, 5000));
  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
  QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  timer.start(6000);
  if (!watcher.isFinished()) loop.exec();
  return watcher.reply();
}

QVariant canSeek(const QDBusConnection& client, const QString& service)
{
  const auto reply = invoke(client, service, "org.freedesktop.DBus.Properties", "Get",
                            {playerInterface, "CanSeek"});
  if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() != 1) return {};
  return qvariant_cast<QDBusVariant>(reply.arguments().first()).variant();
}

bool idle(mpv_handle* handle)
{
  int value = 0;
  return mpv_get_property(handle, "idle-active", MPV_FORMAT_FLAG, &value) >= 0 && value;
}

int brightPixels(const QImage& image)
{
  int count = 0;
  for (int y = 0; y < image.height(); y += 4)
    for (int x = 0; x < image.width(); x += 4)
      if (qGray(image.pixel(x, y)) > 170) ++count;
  return count;
}
}

class TestMprisActiveStop : public QObject
{
  Q_OBJECT
private slots:
  void activeStop();
private:
  void waitForPlayback(MprisOffscreenScene& scene, PlayerComponent& player, const QString& evidence);
  void exerciseBus(const QDBusConnection& client, const QString& service, mpv_handle* handle);
};

void TestMprisActiveStop::waitForPlayback(MprisOffscreenScene& scene, PlayerComponent& player,
                                         const QString& evidence)
{
  QElapsedTimer timer;
  timer.start();
  QImage first;
  while (timer.elapsed() < 10000) {
    first = scene.image();
    if (!first.isNull() && brightPixels(first) > 100 && player.getPosition() > 500) break;
    QTest::qWait(20);
  }
  QVERIFY2(!first.isNull() && brightPixels(first) > 100, "No generated video frame rendered");
  const qint64 before = player.getPosition();
  QVERIFY(before > 500);
  QVERIFY(first.save(evidence + "/active-before.png"));
  QImage second;
  timer.restart();
  while (timer.elapsed() < 3000) {
    second = scene.image();
    if (player.getPosition() > before + 500 && second != first) break;
    QTest::qWait(20);
  }
  QVERIFY(player.getPosition() > before + 500);
  QVERIFY(!second.isNull() && second != first);
  QVERIFY(second.save(evidence + "/active-after.png"));
  qInfo() << "Live generated clip advanced from" << before << "to" << player.getPosition() << "ms";
}

void TestMprisActiveStop::exerciseBus(const QDBusConnection& client, const QString& service,
                                      mpv_handle* handle)
{
  auto& input = InputComponent::Get();
  QSignalSpy forwarded(&input, &InputComponent::hostInput);
  QSignalSpy stops(&input, &InputComponent::nativeStopResult);
  SystemComponent::Get().hello("mpris-active-stop-fixture");
  QCOMPARE(canSeek(client, service), QVariant(true));
  QVERIFY(!idle(handle));
  QCOMPARE(invoke(client, service, playerInterface, "Stop").type(), QDBusMessage::ReplyMessage);
  QCOMPARE(forwarded.count(), 1);
  QCOMPARE(forwarded.at(0).at(0).toStringList(), QStringList{"stop"});
  QCOMPARE(stops.count(), 0);
  QVERIFY(!idle(handle));
  WebClientFaultGuard::instance().observeRendererLoss(0, 17);
  QCOMPARE(canSeek(client, service), QVariant(false));
  QCOMPARE(invoke(client, service, playerInterface, "Stop").type(), QDBusMessage::ReplyMessage);
  QCOMPARE(stops.count(), 1);
  QCOMPARE(stops.at(0).at(0).toInt(), static_cast<int>(PlayerComponent::NativeStopResult::Status::Accepted));
  QCOMPARE(forwarded.count(), 1);
  QTRY_VERIFY_WITH_TIMEOUT(idle(handle), 10000);
  QTest::qWait(1000);
  QVERIFY(idle(handle));
  QCOMPARE(stops.count(), 1);
  QCOMPARE(forwarded.count(), 1);
  qInfo() << "MPRIS detached Stop accepted once; same mpv core remained idle" << handle;
}

void TestMprisActiveStop::activeStop()
{
  const QString root = qEnvironmentVariable("JELLYFIN_TEST_OWNED_ROOT");
  const QString evidence = qEnvironmentVariable("RECOVERY_EVIDENCE_DIR");
  const QString media = qEnvironmentVariable("RECOVERY_MEDIA");
  const QString rootPath = QFileInfo(root).canonicalFilePath();
  const QString evidencePath = QFileInfo(evidence).canonicalFilePath();
  const QString mediaPath = QFileInfo(media).canonicalFilePath();
  QVERIFY(!rootPath.isEmpty() && !evidencePath.isEmpty() && !mediaPath.isEmpty() && QFileInfo(media).isFile());
  const QString relative = QDir(rootPath).relativeFilePath(evidencePath);
  QVERIFY(relative != ".." && !relative.startsWith("../") && !QDir::isAbsolutePath(relative));
  const QString relativeMedia = QDir(rootPath).relativeFilePath(mediaPath);
  QVERIFY(relativeMedia != ".." && !relativeMedia.startsWith("../") && !QDir::isAbsolutePath(relativeMedia));
  QVERIFY2(QDBusConnection::sessionBus().isConnected(), "A private D-Bus session is required");
  auto client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "mpris-active-stop-client");
  QVERIFY(client.isConnected());
  QVERIFY(client.baseService() != QDBusConnection::sessionBus().baseService());
  Q_INIT_RESOURCE(resources);
  QString error;
  QVERIFY2(MpvTestBootstrap::prepareMpvTestEnvironment(root, &error), qPrintable(error));
  QVERIFY2(prepareMpvTestProfile(evidence, &error), qPrintable(error));
  QVERIFY(SettingsComponent::Get().componentInitialize());
  MprisOffscreenScene scene;
  QVERIFY2(scene.initialize(), "Native offscreen OpenGL scene initialization failed");
  QVERIFY(!scene.window->isVisible());
  QSignalSpy ready(scene.video, &MpvAbstractItem::ready);
  auto& player = PlayerComponent::Get();
  QVERIFY(player.componentInitialize());
  player.setWindow(scene.window.get());
  QElapsedTimer timer;
  timer.start();
  while (ready.isEmpty() && !scene.failed && timer.elapsed() < 15000) {
    QVERIFY(scene.frame());
    QTest::qWait(20);
  }
  QVERIFY(!scene.failed && !ready.isEmpty());
  QVERIFY(scene.video->controller() && scene.video->controller()->mpv());
  mpv_handle* handle = scene.video->controller()->mpv();
  QTRY_COMPARE_WITH_TIMEOUT(player.requestNativeStop().status,
                            PlayerComponent::NativeStopResult::Status::Accepted, 15000);
  QVERIFY(player.load(QUrl::fromLocalFile(media).toString(), {{"autoplay", true}}, {{"type", "video"}}));
  waitForPlayback(scene, player, evidence);
  if (QTest::currentTestFailed()) return;
  int paused = 1;
  QCOMPARE(mpv_set_property(handle, "pause", MPV_FORMAT_FLAG, &paused), 0);
  QTRY_VERIFY_WITH_TIMEOUT(mpv_get_property(handle, "pause", MPV_FORMAT_FLAG, &paused) >= 0 && paused, 5000);
  QVERIFY(!idle(handle));
  MprisComponent mpris;
  QVERIFY(mpris.componentInitialize());
  mpris.componentPostInitialize();
  // These are bridge-facing state notifications for the known generated ten-second clip.
  qInfo() << "Seeding MPRIS bridge metadata for generated ten-second clip";
  mpris.notifyDurationChange(10000);
  mpris.notifyPlaybackState("Playing");
  const QString profile = QFileInfo(ProfileManager::activeProfile().cacheDir()).fileName();
  const QString service = "org.mpris.MediaPlayer2.JellyfinDesktop.profile_" + profile;
  exerciseBus(client, service, handle);
}

int main(int argc, char** argv)
{
  QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  QApplication app(argc, argv);
  TestMprisActiveStop test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_mpris_active_stop.moc"
