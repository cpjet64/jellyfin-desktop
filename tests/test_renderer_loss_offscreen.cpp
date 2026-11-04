// Offscreen GL evidence only; this is not a visible-desktop test.
#include <QtTest/QtTest>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QQuickGraphicsDevice>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QUrl>
#include <MpvController>
#include <mpv/client.h>
#include <memory>
#include <cstdio>
#include <cstdlib>

#include "player/MpvVideoItem.h"
#include "player/PlayerComponent.h"
#include "settings/SettingsComponent.h"
#include "system/WebClientFaultGuard.h"
#include "mpv_test_bootstrap.h"
#include "mpv_test_profile.h"

namespace {
class OffscreenScene
{
public:
  bool initialize();
  bool frame();
  QImage image();
  ~OffscreenScene();
  QOpenGLContext context;
  QOffscreenSurface surface;
  std::unique_ptr<QQuickRenderControl> control;
  std::unique_ptr<QQuickWindow> window;
  std::unique_ptr<QOpenGLFramebufferObject> target;
  MpvVideoItem* video = nullptr;
  bool initialized = false;
  bool sceneGraphFailed = false;
  QString sceneGraphMessage;
};

bool OffscreenScene::initialize()
{
  context.setFormat(QSurfaceFormat::defaultFormat());
  if (!context.create() || context.isOpenGLES()) return false;
  surface.setFormat(context.format());
  surface.create();
  if (!surface.isValid() || !context.makeCurrent(&surface)) return false;
  const QSize size(640, 360);
  target = std::make_unique<QOpenGLFramebufferObject>(size);
  if (!target->isValid()) return false;
  auto* gl = context.functions();
  qInfo() << "Offscreen GL vendor:" << reinterpret_cast<const char*>(gl->glGetString(GL_VENDOR))
          << "renderer:" << reinterpret_cast<const char*>(gl->glGetString(GL_RENDERER))
          << "version:" << reinterpret_cast<const char*>(gl->glGetString(GL_VERSION))
          << "context valid:" << context.isValid() << "surface valid:" << surface.isValid()
          << "current context:" << (QOpenGLContext::currentContext() == &context)
          << "target valid:" << target->isValid() << "target texture:" << target->texture();
  control = std::make_unique<QQuickRenderControl>();
  window = std::make_unique<QQuickWindow>(control.get());
  QObject::connect(window.get(), &QQuickWindow::sceneGraphError, window.get(),
                   [this](QQuickWindow::SceneGraphError error, const QString& message) {
    sceneGraphFailed = true;
    sceneGraphMessage = message;
    qCritical() << "Offscreen scene graph error:" << error << message;
  }, Qt::DirectConnection);
  window->resize(size);
  window->setColor(Qt::black);
  window->setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(&context));
  window->setRenderTarget(QQuickRenderTarget::fromOpenGLTexture(target->texture(), size));
  video = new MpvVideoItem(window->contentItem());
  video->setParent(window->contentItem());
  video->setObjectName("video");
  video->setSize(QSizeF(size));
  initialized = control->initialize();
  qInfo() << "Offscreen scene graph graphics API:" << window->rendererInterface()->graphicsApi()
          << "initialized:" << initialized;
  return initialized && !sceneGraphFailed &&
         window->rendererInterface()->graphicsApi() == QSGRendererInterface::OpenGL;
}

bool OffscreenScene::frame()
{
  if (sceneGraphFailed || !context.makeCurrent(&surface)) return false;
  control->polishItems();
  control->beginFrame();
  control->sync();
  control->render();
  control->endFrame();
  return !sceneGraphFailed;
}

QImage OffscreenScene::image()
{
  if (!frame() || !context.makeCurrent(&surface)) return {};
  return target->toImage();
}

OffscreenScene::~OffscreenScene()
{
  if (video || target || initialized) {
    auto requireContext = [this]() {
      if (!context.isValid() || !surface.isValid() || !context.makeCurrent(&surface)) {
        std::fputs("Offscreen cleanup cannot restore its OpenGL context\n", stderr);
        std::fflush(stderr);
        std::_Exit(EXIT_FAILURE);
      }
    };
    requireContext();
    if (control && initialized) control->invalidate();
    requireContext();
    delete video;
    video = nullptr;
    requireContext();
    window.reset();
    requireContext();
    control.reset();
    requireContext();
    target.reset();
    context.doneCurrent();
  }
}

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
  explicit HeldHook(mpv_handle* primary) : client(mpv_create_client(primary, "offscreen-test-hook")) {}
  ~HeldHook() { if (client && active) mpv_hook_continue(client, id); if (client) mpv_destroy(client); }
  bool poll()
  {
    if (active) return true;
    mpv_event* event = mpv_wait_event(client, 0);
    if (!event || event->event_id != MPV_EVENT_HOOK || !event->data) return false;
    id = static_cast<mpv_event_hook*>(event->data)->id;
    return active = true;
  }
  int release() { const int result = mpv_hook_continue(client, id); if (result >= 0) active = false; return result; }
  mpv_handle* client;
private:
  uint64_t id = 0;
  bool active = false;
};
}

class TestRendererLossOffscreen : public QObject
{
  Q_OBJECT
private slots:
  void actualPlayback();
private:
  void heldHook(PlayerComponent& player, mpv_handle* primary, const QString& media);
  void verifyPlayback(PlayerComponent& player, mpv_handle* primary, OffscreenScene& scene,
                      const QString& scenario, const QString& evidence);
};

void TestRendererLossOffscreen::actualPlayback()
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
  const QString relative = QDir(rootPath).relativeFilePath(evidencePath);
  QVERIFY(relative != ".." && !relative.startsWith("../") && !QDir::isAbsolutePath(relative));
  QVERIFY2(MpvTestBootstrap::prepareMpvTestEnvironment(ownedRoot, &setupError), qPrintable(setupError));
  QVERIFY2(prepareMpvTestProfile(evidence, &setupError), qPrintable(setupError));
  QVERIFY(SettingsComponent::Get().componentInitialize());
  OffscreenScene scene;
  QVERIFY2(scene.initialize(), "Unable to initialize native OpenGL offscreen scene");
  QVERIFY(!scene.window->isVisible());
  QSignalSpy ready(scene.video, &MpvAbstractItem::ready);
  auto& player = PlayerComponent::Get();
  QVERIFY(player.componentInitialize());
  player.setWindow(scene.window.get());
  QElapsedTimer deadline;
  deadline.start();
  while (ready.isEmpty() && !scene.sceneGraphFailed && deadline.elapsed() < 15000) {
    QVERIFY(scene.frame());
    QTest::qWait(20);
  }
  QVERIFY2(!scene.sceneGraphFailed, qPrintable(scene.sceneGraphMessage));
  QVERIFY2(!ready.isEmpty(), "Offscreen mpv renderer readiness timed out");
  QTRY_COMPARE_WITH_TIMEOUT(player.requestNativeStop().status,
                            PlayerComponent::NativeStopResult::Status::Accepted, 15000);
  mpv_handle* primary = scene.video->controller()->mpv();
  QVERIFY(primary);
  if (scenario == "held-hook") { heldHook(player, primary, media); return; }
  QVERIFY(player.load(QUrl::fromLocalFile(media).toString(), {{"autoplay", scenario != "paused"}}, {{"type", "video"}}));
  verifyPlayback(player, primary, scene, scenario, evidence);
}

void TestRendererLossOffscreen::heldHook(PlayerComponent& player, mpv_handle* primary, const QString& media)
{
  HeldHook hook(primary);
  QVERIFY(hook.client);
  QCOMPARE(mpv_hook_add(hook.client, 77, "on_load", -10), 0);
  QVERIFY(player.load(QUrl::fromLocalFile(media).toString(), {{"autoplay", true}}, {{"type", "video"}}));
  QTRY_VERIFY_WITH_TIMEOUT(hook.poll(), 10000);
  WebClientFaultGuard::instance().observeRendererLoss(0, 17);
  QCOMPARE(player.requestNativeStop().status, PlayerComponent::NativeStopResult::Status::Accepted);
  QCOMPARE(hook.release(), 0);
  QTRY_VERIFY_WITH_TIMEOUT(readIdle(primary), 10000);
  QTest::qWait(1000);
  QVERIFY(readIdle(primary));
}

void TestRendererLossOffscreen::verifyPlayback(PlayerComponent& player, mpv_handle* primary,
                                                OffscreenScene& scene, const QString& scenario,
                                                const QString& evidence)
{
  QElapsedTimer wait;
  wait.start();
  QImage before;
  while (wait.elapsed() < 10000) {
    before = scene.image();
    if (brightPixels(before) > 100 && (scenario == "paused" || player.getPosition() > 500)) break;
    QTest::qWait(20);
  }
  QVERIFY2(!before.isNull() && brightPixels(before) > 100, "Video not rendered to offscreen target");
  const qint64 beforePosition = player.getPosition();
  if (scenario == "playing") QVERIFY(beforePosition > 500);
  QVERIFY(before.save(evidence + "/offscreen-before.png"));
  WebClientFaultGuard::instance().observeRendererLoss(0, 17);
  wait.restart();
  while (wait.elapsed() < (scenario == "paused" ? 5200 : 1200)) {
    QVERIFY(scene.frame());
    QTest::qWait(20);
  }
  const QImage after = scene.image();
  const qint64 afterPosition = player.getPosition();
  QVERIFY(!after.isNull());
  QVERIFY(after.save(evidence + "/offscreen-after.png"));
  if (scenario == "playing") {
    QVERIFY(afterPosition > beforePosition + 500);
    QVERIFY(before != after);
    QVERIFY2(brightPixels(after) > 100, "First post-fault frame lacks video content");
    wait.restart();
    while (wait.elapsed() < 500) {
      QVERIFY(scene.frame());
      QTest::qWait(20);
    }
    const QImage after2 = scene.image();
    QVERIFY(after2.save(evidence + "/offscreen-after2.png"));
    QVERIFY2(brightPixels(after2) > 100, "Second post-fault frame lacks video content");
    QVERIFY(after != after2);
    QVERIFY(player.getPosition() > afterPosition + 200);
  } else {
    QVERIFY(qAbs(afterPosition - beforePosition) < 100);
    QCOMPARE(before, after);
  }
  QCOMPARE(player.requestNativeStop().status, PlayerComponent::NativeStopResult::Status::Accepted);
  QTRY_VERIFY_WITH_TIMEOUT(readIdle(primary), 10000);
}

int main(int argc, char** argv)
{
  QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  QGuiApplication app(argc, argv);
  TestRendererLossOffscreen test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_renderer_loss_offscreen.moc"
