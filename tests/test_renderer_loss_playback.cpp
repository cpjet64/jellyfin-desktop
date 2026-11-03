// Run each RECOVERY_SCENARIO in a separate, externally contained process.
// The secondary mpv client below consumes only its own hook events; it never
// reads the PlayerComponent client's event queue.
// This fixture does not itself exercise stale WebChannel messages or count all
// mpv commands; those require separate transport and command-observation proof.
#include <QtTest/QtTest>
#include <QGuiApplication>
#include <QFileInfo>
#include <QImage>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QUrl>
#include <MpvController>
#include <mpv/client.h>

#include "player/MpvVideoItem.h"
#include "player/PlayerComponent.h"
#include "system/WebClientFaultGuard.h"

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

mpv_event_hook* nextHook(mpv_handle* client)
{
  mpv_event* event = mpv_wait_event(client, 0);
  return event && event->event_id == MPV_EVENT_HOOK ? static_cast<mpv_event_hook*>(event->data) : nullptr;
}
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
  QTRY_VERIFY_WITH_TIMEOUT(ready.count() > 0, 15000);
  QVERIFY(video->controller());
  mpv_handle* primary = video->controller()->mpv();
  QVERIFY(primary);

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
  mpv_handle* hookClient = mpv_create_client(primary, "recovery-test-hook");
  QVERIFY(hookClient);
  QCOMPARE(mpv_hook_add(hookClient, 77, "on_load", -10), 0);
  QVERIFY(player.load(QUrl::fromLocalFile(media).toString(), {{"autoplay", true}}, {{"type", "video"}}));
  mpv_event_hook* hook = nullptr;
  QTRY_VERIFY_WITH_TIMEOUT((hook = nextHook(hookClient)) != nullptr, 10000);
  const uint64_t hookId = hook->id;
  WebClientFaultGuard::instance().observeRendererLoss(0, 17);
  const auto stopped = player.requestNativeStop();
  QCOMPARE(stopped.status, PlayerComponent::NativeStopResult::Status::Accepted);
  QCOMPARE(mpv_hook_continue(hookClient, hookId), 0);
  QTRY_VERIFY_WITH_TIMEOUT(readIdle(primary), 10000);
  QTest::qWait(1000);
  QVERIFY(readIdle(primary));
  mpv_destroy(hookClient);
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
  QTest::qWait(1200);
  const qint64 afterPosition = player.getPosition();
  const QImage after = window.grabWindow();
  QVERIFY(!after.isNull());
  QVERIFY(after.save(evidence + "/after.png"));
  if (scenario == "playing") {
    QVERIFY(afterPosition > beforePosition + 500);
    QVERIFY(before != after);
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
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  QGuiApplication app(argc, argv);
  TestRendererLossPlayback test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_renderer_loss_playback.moc"
