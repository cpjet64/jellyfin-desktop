#include <QtTest/QtTest>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QUrl>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <MpvController>
#include <mpv/client.h>
#include <memory>

#include "input/InputComponent.h"
#include "player/MpvVideoItem.h"
#include "player/PlayerComponent.h"
#include "settings/SettingsComponent.h"
#include "shared/Paths.h"
#include "system/WebClientFaultGuard.h"
#include "mpv_test_bootstrap.h"
#include "mpv_test_profile.h"

class Hook
{
public:
  explicit Hook(mpv_handle* primary) : client(mpv_create_client(primary, "web-containment-hook")) {}
  ~Hook() { if (client && active) mpv_hook_continue(client, id); if (client) mpv_destroy(client); }
  bool poll()
  {
    if (active) return true;
    while (mpv_event* event = mpv_wait_event(client, 0)) {
      if (event->event_id == MPV_EVENT_NONE) break;
      if (event->event_id == MPV_EVENT_HOOK && event->data) {
        id = static_cast<mpv_event_hook*>(event->data)->id;
        return active = true;
      }
    }
    return false;
  }
  bool release() { if (mpv_hook_continue(client, id) < 0) return false; active = false; return true; }
  mpv_handle* client = nullptr;
  bool active = false;
private:
  uint64_t id = 0;
};

static bool idle(mpv_handle* mpv)
{
  int value = 0;
  return mpv_get_property(mpv, "idle-active", MPV_FORMAT_FLAG, &value) >= 0 && value;
}

static int brightPixels(const QImage& image)
{
  int count = 0;
  for (int y = 0; y < image.height(); y += 4)
    for (int x = 0; x < image.width(); x += 4)
      if (qGray(image.pixel(x, y)) > 170) ++count;
  return count;
}

static bool visibleVideo(const QImage& image)
{
  if (image.isNull()) return false;
  const int samples = ((image.width() + 3) / 4) * ((image.height() + 3) / 4);
  const int bright = brightPixels(image);
  // The generated clip starts with one 20x20 bright square and no lit counter
  // bits; its first frame occupies about 2% of the video area.
  return bright > samples / 70 && bright < samples * 3 / 4;
}

static QImage waitForVideo(QQuickView& view, const QString& path)
{
  QElapsedTimer timer;
  timer.start();
  QImage frame;
  do {
    frame = view.grabWindow();
    if (visibleVideo(frame)) break;
    QTest::qWait(100);
  } while (timer.elapsed() < 10000);
  if (!frame.isNull()) frame.save(path);
  return frame;
}

static bool mediaReady(mpv_handle* mpv, bool paused)
{
  int isPaused = 0;
  int64_t width = 0;
  return !idle(mpv) && mpv_get_property(mpv, "pause", MPV_FORMAT_FLAG, &isPaused) >= 0 &&
         bool(isPaused) == paused && mpv_get_property(mpv, "video-params/w", MPV_FORMAT_INT64, &width) >= 0 && width > 0;
}

static QString jsonString(const QString& value)
{
  QByteArray data = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
  return QString::fromUtf8(data.mid(1, data.size() - 2));
}

#ifdef PLAYER_INTENT_DIAGNOSTIC_ONLY
#include "synchronous_stop_case.h"
#endif

class TestWebClientContainment : public QObject
{
  Q_OBJECT
private slots:
  void containment();
private:
  void connectBridge(QObject* root, PlayerComponent& player, SettingsComponent& settings);
  void checkProxies(QObject* root);
  void preflightInputAndSettings(QObject* root, SettingsComponent& settings, int& hostCalls);
  void attemptStaleCalls(QObject* root, const QString& mediaUrl, bool desiredSetting);
  void verifyHeld(PlayerComponent& player, mpv_handle* primary, Hook& hook);
  void verifyPlaying(PlayerComponent& player, mpv_handle* primary, Hook& hook, QQuickView& view,
                     const QString& scenario, qint64 before, const QImage& frame, const QString& evidence);
  void exercisePlayback(QObject* root, QQuickView& view, mpv_handle* primary,
                        const QString& scenario, const QString& media, const QString& evidence);
};

void TestWebClientContainment::connectBridge(QObject* root, PlayerComponent& player, SettingsComponent& settings)
{
  QVERIFY(QMetaObject::invokeMethod(root, "registerComponents",
                                   Q_ARG(QVariant, QVariant::fromValue(static_cast<QObject*>(&player))),
                                   Q_ARG(QVariant, QVariant::fromValue(static_cast<QObject*>(&settings)))));
  QVERIFY(QMetaObject::invokeMethod(root, "loadPage"));
  QTRY_COMPARE_WITH_TIMEOUT(root->property("loads").toInt(), 1, 15000);
  QVERIFY(QMetaObject::invokeMethod(root, "connectBridge"));
  QTRY_VERIFY_WITH_TIMEOUT((QMetaObject::invokeMethod(root, "checkReady"),
                            root->property("bridgeReady").toBool()), 15000);
}

void TestWebClientContainment::preflightInputAndSettings(QObject* root, SettingsComponent& settings, int& hostCalls)
{
  auto& input = InputComponent::Get();
  QSignalSpy seeks(&input, &InputComponent::positionSeek);
  const bool oldSetting = settings.value("main", "enableInputRepeat").toBool();
  const QString code = QStringLiteral("fixtureInput.executeActions(['host:containment-probe']);"
      "fixtureInput.seekTo(100); fixtureSettings.setValue('main','enableInputRepeat',%1); 'sent'")
      .arg(oldSetting ? QStringLiteral("false") : QStringLiteral("true"));
  QVERIFY(QMetaObject::invokeMethod(root, "evaluate", Q_ARG(QVariant, QVariant(code))));
  QTRY_COMPARE_WITH_TIMEOUT(hostCalls, 1, 5000);
  QTRY_COMPARE_WITH_TIMEOUT(seeks.count(), 1, 5000);
  QTRY_VERIFY_WITH_TIMEOUT(settings.value("main", "enableInputRepeat").toBool() != oldSetting, 5000);
  settings.setValue("main", "enableInputRepeat", oldSetting);
  hostCalls = 0;
}

void TestWebClientContainment::checkProxies(QObject* root)
{
  const QString code = QStringLiteral("['seekTo','stop','load'].every(k => typeof fixturePlayer[k] === 'function')"
      " && typeof fixtureInput.executeActions === 'function' && typeof fixtureInput.seekTo === 'function'"
      " && typeof fixtureSettings.setValue === 'function'");
  QVERIFY(QMetaObject::invokeMethod(root, "evaluate", Q_ARG(QVariant, QVariant(code))));
  QTRY_COMPARE_WITH_TIMEOUT(root->property("result").toString(), QStringLiteral("true"), 5000);
}

void TestWebClientContainment::attemptStaleCalls(QObject* root, const QString& mediaUrl, bool desiredSetting)
{
  const QString code = QStringLiteral("let n=0; const calls=["
      "()=>fixturePlayer.seekTo(8500),()=>fixturePlayer.stop(),"
      "()=>fixturePlayer.load(%1,{autoplay:true},{type:'video'},null,null),"
      "()=>fixtureInput.executeActions(['host:containment-probe']),()=>fixtureInput.seekTo(8500),"
      "()=>fixtureSettings.setValue('main','enableInputRepeat',%2)];"
      "for (const call of calls) { try { call(); } catch(e) {} n++; } n;")
      .arg(jsonString(mediaUrl), desiredSetting ? QStringLiteral("true") : QStringLiteral("false"));
  QVERIFY(QMetaObject::invokeMethod(root, "evaluate", Q_ARG(QVariant, QVariant(code))));
  QTRY_COMPARE_WITH_TIMEOUT(root->property("result").toString(), QStringLiteral("6"), 5000);
}

void TestWebClientContainment::verifyHeld(PlayerComponent& player, mpv_handle* primary, Hook& hook)
{
  QVERIFY(hook.active);
  QCOMPARE(player.requestNativeStop().status, PlayerComponent::NativeStopResult::Status::Accepted);
  QVERIFY(hook.release());
  QTRY_VERIFY_WITH_TIMEOUT(idle(primary), 10000);
  QTest::qWait(500);
  QVERIFY(idle(primary));
}

void TestWebClientContainment::verifyPlaying(PlayerComponent& player, mpv_handle* primary, Hook& hook,
                                              QQuickView& view, const QString& scenario, qint64 before,
                                              const QImage& frame, const QString& evidence)
{
  QTest::qWait(600);
  QVERIFY(!idle(primary));
  QVERIFY(!hook.poll()); // No stale load reached the mpv on_load hook.
  const qint64 after = player.getPosition();
  const QImage afterFrame = view.grabWindow();
  QVERIFY(!afterFrame.isNull());
  QVERIFY(visibleVideo(afterFrame));
  QVERIFY(afterFrame.save(evidence + "/containment-after.png"));
  if (scenario == "playing") {
    QVERIFY(after > before + 200);
    QVERIFY(after < before + 3000);
    QVERIFY(frame != afterFrame);
  } else {
    QVERIFY(qAbs(after - before) < 100);
    QCOMPARE(frame, afterFrame);
  }
  QCOMPARE(player.requestNativeStop().status, PlayerComponent::NativeStopResult::Status::Accepted);
  QTRY_VERIFY_WITH_TIMEOUT(idle(primary), 10000);
}

void TestWebClientContainment::exercisePlayback(QObject* root, QQuickView& view, mpv_handle* primary,
                                                 const QString& scenario, const QString& media,
                                                 const QString& evidence)
{
  auto& player = PlayerComponent::Get();
  auto& settings = SettingsComponent::Get();
  auto& input = InputComponent::Get();
  auto hostCalls = std::make_shared<int>(0);
  input.registerHostCommand("containment-probe", [hostCalls] { ++*hostCalls; });
  preflightInputAndSettings(root, settings, *hostCalls);
  QVERIFY(!QTest::currentTestFailed());
  Hook hook(primary);
  QVERIFY(hook.client);
  if (scenario == "held-hook") QCOMPARE(mpv_hook_add(hook.client, 77, "on_load", -10), 0);
  const QString mediaUrl = QUrl::fromLocalFile(media).toString();
  QVERIFY(player.load(mediaUrl, {{"autoplay", scenario != "paused"}}, {{"type", "video"}}));
  if (scenario == "held-hook") QTRY_VERIFY_WITH_TIMEOUT(hook.poll(), 10000);
  else {
    QTRY_VERIFY_WITH_TIMEOUT(mediaReady(primary, scenario == "paused"), 10000);
    if (scenario == "playing") QTRY_VERIFY_WITH_TIMEOUT(player.getPosition() > 500, 10000);
  }
  if (scenario != "held-hook") QCOMPARE(mpv_hook_add(hook.client, 77, "on_load", -10), 0);
  const QImage frame = scenario == "held-hook" ? QImage() : waitForVideo(view, evidence + "/containment-before.png");
  const qint64 before = player.getPosition();
  if (scenario != "held-hook") {
    QVERIFY(!frame.isNull());
    QVERIFY(visibleVideo(frame));
    QVERIFY(frame.save(evidence + "/containment-before.png"));
  }
  const bool oldSetting = settings.value("main", "enableInputRepeat").toBool();
  QSignalSpy seeks(&input, &InputComponent::positionSeek);
  QSignalSpy settingsChanges(&settings, &SettingsComponent::sectionValueUpdate);
  QVERIFY(QMetaObject::invokeMethod(root, "loseRenderer"));
  QVERIFY(WebClientFaultGuard::instance().rendererLost());
  attemptStaleCalls(root, mediaUrl, !oldSetting);
  QVERIFY(!QTest::currentTestFailed());
  QCOMPARE(*hostCalls, 0);
  QCOMPARE(seeks.count(), 0);
  QCOMPARE(settings.value("main", "enableInputRepeat").toBool(), oldSetting);
  QCOMPARE(settingsChanges.count(), 0);
  if (scenario == "held-hook") verifyHeld(player, primary, hook);
  else verifyPlaying(player, primary, hook, view, scenario, before, frame, evidence);
}

void TestWebClientContainment::containment()
{
  const QString scenario = qEnvironmentVariable("RECOVERY_SCENARIO");
  const QString media = qEnvironmentVariable("RECOVERY_MEDIA");
  const QString evidence = qEnvironmentVariable("RECOVERY_EVIDENCE_DIR");
  QVERIFY(scenario == "playing" || scenario == "paused" || scenario == "held-hook" || scenario == "synchronous-stop");
  if (scenario == "synchronous-stop")
    QFAIL("HELD: windowed render, synchronous controls and teardown require independent qualification");
  qInfo().noquote() << "RECOVERY_SCENARIO=" + scenario;
  QVERIFY(QFileInfo::exists(media));
  QVERIFY(QFileInfo(evidence).isDir());
  QTemporaryDir profile(QDir::currentPath() + "/web-containment-XXXXXX");
  QVERIFY(profile.isValid());
  QString error;
  QVERIFY2(prepareMpvTestProfile(profile.path(), &error), qPrintable(error));
  Q_INIT_RESOURCE(resources);
  auto& settings = SettingsComponent::Get();
  QVERIFY(settings.componentInitialize());
  auto& input = InputComponent::Get();
  auto& player = PlayerComponent::Get();
  auto& guard = WebClientFaultGuard::instance();
  QQuickView view;
  view.rootContext()->setContextProperty("webClientFaultGuard", &guard);
  view.rootContext()->setContextProperty("fixtureInput", &input);
  view.setSource(QUrl::fromLocalFile(QStringLiteral(TEST_SOURCE_DIR "/webclient_fault_fixture.qml")));
  QCOMPARE(view.status(), QQuickView::Ready);
  QObject* root = view.rootObject();
  QVERIFY(root);
  auto* video = new MpvVideoItem(view.contentItem());
  video->setParent(view.contentItem());
  video->setObjectName("video");
  video->setSize(QSizeF(320, 240));
  video->setZ(-1);
  QSignalSpy ready(video, &MpvAbstractItem::ready);
  QVERIFY(player.componentInitialize());
  player.setWindow(&view);
  view.show();
  QTRY_VERIFY_WITH_TIMEOUT(ready.count() > 0, 15000);
  QVERIFY(video->controller());
  mpv_handle* primary = video->controller()->mpv();
  QVERIFY(primary);
  const auto readinessStop = player.requestNativeStop();
  QCOMPARE(readinessStop.status, PlayerComponent::NativeStopResult::Status::Accepted);
  connectBridge(root, player, settings);
  QVERIFY(!QTest::currentTestFailed());
  checkProxies(root);
  QVERIFY(!QTest::currentTestFailed());
  if (scenario == "synchronous-stop") {
#ifdef PLAYER_INTENT_DIAGNOSTIC_ONLY
    runSynchronousStop(root, view, player, primary, media, evidence);
#else
    QFAIL("synchronous-stop requires the diagnostic observer build");
#endif
    return;
  }
  exercisePlayback(root, view, primary, scenario, media, evidence);
}

int main(int argc, char** argv)
{
  if (qEnvironmentVariable("RECOVERY_SCENARIO") == "synchronous-stop") {
    qCritical("HELD: windowed render, synchronous controls and teardown require independent qualification");
    return 1;
  }
  QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  QtWebEngineQuick::initialize();
  QGuiApplication app(argc, argv);
  QTemporaryDir scratch(QDir::currentPath() + "/mpv-bootstrap-XXXXXX");
  QString error;
  const QString root = qEnvironmentVariable("JELLYFIN_TEST_OWNED_ROOT", scratch.path());
  if (!scratch.isValid() || !MpvTestBootstrap::prepareMpvTestEnvironment(root, &error)) {
    qCritical().noquote() << "Mpv test bootstrap failed:" << error;
    return 1;
  }
  TestWebClientContainment test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_webclient_containment.moc"
