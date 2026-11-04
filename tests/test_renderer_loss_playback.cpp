// Run each RECOVERY_SCENARIO in a separate, externally contained process.
// The secondary mpv client below consumes only its own hook events; it never
// reads the PlayerComponent client's event queue.
// This fixture does not itself exercise stale WebChannel messages or count all
// mpv commands; those require separate transport and command-observation proof.
#include <QtTest/QtTest>
#include <QGuiApplication>
#include <QFileInfo>
#include <QImage>
#include <QDir>
#include <QElapsedTimer>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QUrl>
#include <MpvController>
#include <mpv/client.h>

#include "player/MpvVideoItem.h"
#include "player/PlayerComponent.h"
#include "system/WebClientFaultGuard.h"
#include "shared/Paths.h"
#include "settings/SettingsComponent.h"
#include "mpv_test_bootstrap.h"
#include "mpv_test_profile.h"
#include "render_startup_diagnostics.h"

namespace {
int brightPixels(const QImage& image)
{
  int count = 0;
  for (int y = 0; y < image.height(); y += 4)
    for (int x = 0; x < image.width(); x += 4)
      if (qGray(image.pixel(x, y)) > 170) ++count;
  return count;
}

bool readIdle(mpv_handle* mpv)
{
  int idle = 0;
  return mpv_get_property(mpv, "idle-active", MPV_FORMAT_FLAG, &idle) >= 0 && idle;
}

class HeldHook
{
public:
  explicit HeldHook(mpv_handle* primary) : client(mpv_create_client(primary, "recovery-test-hook")) {}
  ~HeldHook()
  {
    if (client && active) mpv_hook_continue(client, id);
    if (client) mpv_destroy(client);
  }
  bool poll()
  {
    if (active) return true;
    mpv_event* event = mpv_wait_event(client, 0);
    if (!event || event->event_id != MPV_EVENT_HOOK || !event->data) return false;
    id = static_cast<mpv_event_hook*>(event->data)->id;
    active = true;
    return true;
  }
  int release()
  {
    const int result = mpv_hook_continue(client, id);
    if (result >= 0) active = false;
    return result;
  }
  mpv_handle* client;
private:
  uint64_t id = 0;
  bool active = false;
};
}

class TestRendererLossPlayback : public QObject
{
  Q_OBJECT
private slots:
  void actualPlayback();
private:
  void heldHook(PlayerComponent& player, mpv_handle* primary, const QString& media);
  void verifyPlayback(PlayerComponent& player, mpv_handle* primary, QQuickWindow& window,
                      const QString& scenario, const QString& evidence);
};

void TestRendererLossPlayback::actualPlayback()
{
  const QString scenario = qEnvironmentVariable("RECOVERY_SCENARIO");
  const QString media = qEnvironmentVariable("RECOVERY_MEDIA");
  const QString evidence = qEnvironmentVariable("RECOVERY_EVIDENCE_DIR");
  QVERIFY(scenario == "playing" || scenario == "paused" || scenario == "held-hook");
  QVERIFY(QFileInfo::exists(media));
  QVERIFY(QFileInfo(evidence).isDir());
  Q_INIT_RESOURCE(resources);
  QString setupError;
  const QString configuredRoot = qEnvironmentVariable("JELLYFIN_TEST_OWNED_ROOT");
  const QString ownedRoot = configuredRoot.isEmpty() ? evidence : configuredRoot;
  const QString rootPath = QFileInfo(ownedRoot).canonicalFilePath();
  const QString evidencePath = QFileInfo(evidence).canonicalFilePath();
  QVERIFY(!rootPath.isEmpty() && !evidencePath.isEmpty());
  const QString relativeEvidence = QDir(rootPath).relativeFilePath(evidencePath);
  QVERIFY(relativeEvidence != ".." && !relativeEvidence.startsWith("../") && !QDir::isAbsolutePath(relativeEvidence));
  QVERIFY2(MpvTestBootstrap::prepareMpvTestEnvironment(ownedRoot, &setupError), qPrintable(setupError));
  QVERIFY2(prepareMpvTestProfile(evidence, &setupError), qPrintable(setupError));
  QVERIFY(SettingsComponent::Get().componentInitialize());

  RenderStartupDiagnostics diagnostics;
  QQuickWindow window;
  window.resize(640, 360);
  auto* video = new MpvVideoItem(window.contentItem());
  video->setParent(window.contentItem());
  video->setObjectName("video");
  video->setSize(QSizeF(640, 360));
  diagnostics.observe(window, *video);
  auto& player = PlayerComponent::Get();
  QVERIFY(player.componentInitialize());
  player.setWindow(&window);
  QVERIFY(diagnostics.save(evidence + "/startup-before-show.json", window, *video));
  window.show();
  QElapsedTimer deadline;
  deadline.start();
  while (!diagnostics.ready() && !diagnostics.failed() && deadline.elapsed() < 15000)
    QTest::qWait(20);
  QVERIFY(diagnostics.save(evidence + "/startup-result.json", window, *video));
  QVERIFY2(!diagnostics.failed(), "Scene graph reported an error; see startup-result.json");
  QVERIFY2(diagnostics.ready(), "Video ready timed out; see startup-result.json");
  QVERIFY(video->controller());
  mpv_handle* primary = video->controller()->mpv();
  QVERIFY(primary);
  // This setup-only stop is an observable readiness check before media loads.
  // The queued ready signal alone can precede PlayerComponent initialization.
  QTRY_COMPARE_WITH_TIMEOUT(player.requestNativeStop().status,
                            PlayerComponent::NativeStopResult::Status::Accepted, 15000);

  if (scenario == "held-hook") {
    heldHook(player, primary, media);
    return;
  }
  const QVariantMap options{{"autoplay", scenario != "paused"}};
  QVERIFY(player.load(QUrl::fromLocalFile(media).toString(), options, {{"type", "video"}}));
  verifyPlayback(player, primary, window, scenario, evidence);
}

void TestRendererLossPlayback::heldHook(PlayerComponent& player, mpv_handle* primary, const QString& media)
{
  HeldHook hook(primary);
  QVERIFY(hook.client);
  QCOMPARE(mpv_hook_add(hook.client, 77, "on_load", -10), 0);
  QVERIFY(player.load(QUrl::fromLocalFile(media).toString(), {{"autoplay", true}}, {{"type", "video"}}));
  QTRY_VERIFY_WITH_TIMEOUT(hook.poll(), 10000);
  WebClientFaultGuard::instance().observeRendererLoss(0, 17);
  const auto stopped = player.requestNativeStop();
  QCOMPARE(stopped.status, PlayerComponent::NativeStopResult::Status::Accepted);
  QCOMPARE(hook.release(), 0);
  QTRY_VERIFY_WITH_TIMEOUT(readIdle(primary), 10000);
  QTest::qWait(1000);
  QVERIFY(readIdle(primary));
}

void TestRendererLossPlayback::verifyPlayback(PlayerComponent& player, mpv_handle* primary,
                                               QQuickWindow& window, const QString& scenario,
                                               const QString& evidence)
{
  if (scenario == "playing") QTRY_VERIFY_WITH_TIMEOUT(player.getPosition() > 500, 10000);
  else QTest::qWait(2000);
  QTRY_VERIFY_WITH_TIMEOUT(brightPixels(window.grabWindow()) > 100, 10000);
  const qint64 beforePosition = player.getPosition();
  const QImage before = window.grabWindow();
  QVERIFY(!before.isNull());
  QVERIFY2(brightPixels(before) > 100, "Video frame not visibly rendered");
  QVERIFY(before.save(evidence + "/before.png"));
  WebClientFaultGuard::instance().observeRendererLoss(0, 17);
  QTest::qWait(scenario == "paused" ? 5200 : 1200);
  const qint64 afterPosition = player.getPosition();
  const QImage after = window.grabWindow();
  QVERIFY(!after.isNull());
  QVERIFY(after.save(evidence + "/after.png"));
  if (scenario == "playing") {
    QVERIFY(afterPosition > beforePosition + 500);
    QVERIFY(before != after);
    QVERIFY2(brightPixels(after) > 100, "First post-fault frame lacks video content");
    QTest::qWait(500);
    const QImage after2 = window.grabWindow();
    QVERIFY(after2.save(evidence + "/after2.png"));
    QVERIFY2(brightPixels(after2) > 100, "Second post-fault frame lacks video content");
    QVERIFY(after != after2);
    QVERIFY(player.getPosition() > afterPosition + 200);
  } else {
    QVERIFY(qAbs(afterPosition - beforePosition) < 100);
    QCOMPARE(before, after);
  }
  const auto stopped = player.requestNativeStop();
  QCOMPARE(stopped.status, PlayerComponent::NativeStopResult::Status::Accepted);
  QTRY_VERIFY_WITH_TIMEOUT(readIdle(primary), 10000);
}

int main(int argc, char** argv)
{
  QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  QGuiApplication app(argc, argv);
  TestRendererLossPlayback test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_renderer_loss_playback.moc"
