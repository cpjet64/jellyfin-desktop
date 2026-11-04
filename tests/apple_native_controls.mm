// Injects the shared production delegate methods; this does not simulate OS event delivery.
#include <QtTest/QtTest>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QThread>
#include <QUrl>
#include <MpvController>
#include <mpv/client.h>
#include <cmath>
#include <limits>

#include "input/InputComponent.h"
#include "input/InputMapping.h"
#include "input/apple/InputAppleMediaKeys.h"
#include "player/MpvVideoItem.h"
#include "player/PlayerComponent.h"
#include "settings/SettingsComponent.h"
#include "system/WebClientFaultGuard.h"
#include "mpv_test_bootstrap.h"
#include "mpv_test_profile.h"

#import <MediaPlayer/MediaPlayer.h>

// Test-only declaration of the actual delegate class and its shared methods.
@interface MediaKeysDelegate : NSObject
-(instancetype)initWithInput:(InputAppleMediaKeys*)input;
-(MPRemoteCommandHandlerStatus)handleCommand:(MPRemoteCommand*)command;
-(MPRemoteCommandHandlerStatus)handlePlaybackPosition:(double)seconds;
@end

static double position(mpv_handle* handle)
{
  double seconds = -1;
  return mpv_get_property(handle, "time-pos", MPV_FORMAT_DOUBLE, &seconds) < 0 ? -1 : seconds;
}

static bool idle(mpv_handle* handle)
{
  int value = 0;
  return mpv_get_property(handle, "idle-active", MPV_FORMAT_FLAG, &value) >= 0 && value;
}

class AppleNativeControls : public QObject
{
  Q_OBJECT
private slots:
  void delegatePath();
private:
  void invalidSeeks(MediaKeysDelegate* delegate, mpv_handle* handle);
  void detachedStop(MediaKeysDelegate* delegate, mpv_handle* handle, InputComponent& input);
};

void AppleNativeControls::invalidSeeks(MediaKeysDelegate* delegate, mpv_handle* handle)
{
  const double before = position(handle);
  QVERIFY(before >= 0);
  const double invalid[] = {-1.0, std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::infinity(),
                            static_cast<double>(std::numeric_limits<qint64>::max())};
  for (double value : invalid)
    QCOMPARE([delegate handlePlaybackPosition:value], MPRemoteCommandHandlerStatusCommandFailed);
  MPRemoteCommandHandlerStatus status = MPRemoteCommandHandlerStatusSuccess;
  auto* worker = QThread::create([&]() { status = [delegate handlePlaybackPosition:1.0]; });
  worker->start();
  QVERIFY(worker->wait(5000));
  delete worker;
  QCOMPARE(status, MPRemoteCommandHandlerStatusCommandFailed);
  QTest::qWait(500);
  QVERIFY(qAbs(position(handle) - before) < 0.1);
}

void AppleNativeControls::detachedStop(MediaKeysDelegate* delegate, mpv_handle* handle,
                                       InputComponent& input)
{
  QSignalSpy stopped(&input, &InputComponent::nativeStopResult);
  QSignalSpy forwarded(&input, &InputComponent::hostInput);
  auto* command = [MPRemoteCommandCenter sharedCommandCenter].changePlaybackPositionCommand;
  command.enabled = YES;
  const double before = position(handle);
  WebClientFaultGuard::instance().observeRendererLoss(0, 17);
  QVERIFY(!command.enabled);
  QCOMPARE([delegate handlePlaybackPosition:7.0], MPRemoteCommandHandlerStatusCommandFailed);
  QTest::qWait(500);
  QVERIFY(qAbs(position(handle) - before) < 0.1);
  QCOMPARE([delegate handleCommand:[MPRemoteCommandCenter sharedCommandCenter].stopCommand],
           MPRemoteCommandHandlerStatusSuccess);
  QCOMPARE(stopped.count(), 1);
  QCOMPARE(stopped.at(0).at(0).toInt(), static_cast<int>(PlayerComponent::NativeStopResult::Status::Accepted));
  QCOMPARE(forwarded.count(), 0);
  QTRY_VERIFY_WITH_TIMEOUT(idle(handle), 10000);
  QCOMPARE(stopped.count(), 1);
}

void AppleNativeControls::delegatePath()
{
  const QString root = qEnvironmentVariable("JELLYFIN_TEST_OWNED_ROOT");
  const QString evidence = qEnvironmentVariable("RECOVERY_EVIDENCE_DIR");
  const QString media = qEnvironmentVariable("RECOVERY_MEDIA");
  const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
  const QString canonicalEvidence = QFileInfo(evidence).canonicalFilePath();
  QVERIFY(!canonicalRoot.isEmpty() && !canonicalEvidence.isEmpty() && QFileInfo(media).isFile());
  const QString relative = QDir(canonicalRoot).relativeFilePath(canonicalEvidence);
  QVERIFY(relative != ".." && !relative.startsWith("../") && !QDir::isAbsolutePath(relative));
  Q_INIT_RESOURCE(resources);
  QString error;
  QVERIFY2(MpvTestBootstrap::prepareMpvTestEnvironment(root, &error), qPrintable(error));
  QVERIFY2(prepareMpvTestProfile(evidence, &error), qPrintable(error));
  QVERIFY(SettingsComponent::Get().componentInitialize());
  auto& input = InputComponent::Get();
  auto* mapping = input.findChild<InputMapping*>();
  QVERIFY(mapping && mapping->loadMappings());
  InputAppleMediaKeys source;
  QVERIFY(source.initInput());
  bool dispatched = false;
  QObject::connect(&source, &InputBase::receivedInput, &input,
                   [&](const QString& name, const QString& key, InputBase::InputkeyState state) {
    dispatched = QMetaObject::invokeMethod(&input, "remapInput", Qt::DirectConnection,
        Q_ARG(QString, name), Q_ARG(QString, key), Q_ARG(InputBase::InputkeyState, state));
  });
  MediaKeysDelegate* delegate = (__bridge MediaKeysDelegate*)source.m_delegate;
  QVERIFY(delegate);
  QQuickWindow window;
  window.resize(640, 360);
  auto* video = new MpvVideoItem(window.contentItem());
  video->setParent(window.contentItem());
  video->setObjectName("video");
  video->setSize(QSizeF(640, 360));
  QSignalSpy ready(video, &MpvAbstractItem::ready);
  auto& player = PlayerComponent::Get();
  QVERIFY(player.componentInitialize());
  player.setWindow(&window);
  window.show();
  QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty(), 15000);
  QVERIFY(video->controller() && video->controller()->mpv());
  mpv_handle* handle = video->controller()->mpv();
  QTRY_COMPARE_WITH_TIMEOUT(player.requestNativeStop().status,
                            PlayerComponent::NativeStopResult::Status::Accepted, 15000);
  QVERIFY(player.load(QUrl::fromLocalFile(media).toString(), {{"autoplay", false}}, {{"type", "video"}}));
  QTRY_VERIFY_WITH_TIMEOUT(position(handle) >= 0, 15000);
  qInfo() << "delegate thread" << source.thread() << "current" << QThread::currentThread()
          << "player" << &player << "mpv" << handle << "media" << media;
  QCOMPARE(QThread::currentThread(), source.thread());
  QCOMPARE([delegate handlePlaybackPosition:4.0], MPRemoteCommandHandlerStatusSuccess);
  QTRY_VERIFY_WITH_TIMEOUT(qAbs(position(handle) - 4.0) < 0.25, 10000);
  invalidSeeks(delegate, handle);
  detachedStop(delegate, handle, input);
  QVERIFY(dispatched);
}

int main(int argc, char** argv)
{
  QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
  QGuiApplication app(argc, argv);
  AppleNativeControls test;
  return QTest::qExec(&test, argc, argv);
}

#include "apple_native_controls.moc"
