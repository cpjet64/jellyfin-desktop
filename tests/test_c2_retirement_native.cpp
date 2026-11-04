// Diagnostic-only fixture: the test retains the item, core and GL context
// until retirement ACK. Run each C2_SCENARIO in a separate contained process.
#include <QtTest/QtTest>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QQuickGraphicsDevice>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QUrl>
#include <MpvController>
#include <mpv/client.h>
#include <cstdlib>
#include <memory>
#include <thread>
#include <chrono>

#include "c2/DiagnosticFailureInjection.h"
#include "player/MpvVideoItem.h"
#include "player/PlayerComponent.h"
#include "settings/SettingsComponent.h"
#include "mpv_test_bootstrap.h"
#include "mpv_test_profile.h"

namespace {
class Scene
{
public:
  bool init();
  bool frame();
  bool waitFor(const std::function<bool()>& predicate, int ms);
  bool destroyItem();
  ~Scene();
  QOpenGLContext context;
  QOffscreenSurface surface;
  std::unique_ptr<QQuickRenderControl> control;
  std::unique_ptr<QQuickWindow> window;
  std::unique_ptr<QOpenGLFramebufferObject> target;
  MpvVideoItem* item = nullptr;
  bool mayHaveContext = false;
  bool retired = false;
};

bool Scene::init()
{
  if (!context.create() || context.isOpenGLES()) return false;
  surface.setFormat(context.format());
  surface.create();
  if (!surface.isValid() || !context.makeCurrent(&surface)) return false;
  target = std::make_unique<QOpenGLFramebufferObject>(QSize(640, 360));
  if (!target->isValid()) return false;
  control = std::make_unique<QQuickRenderControl>();
  window = std::make_unique<QQuickWindow>(control.get());
  window->resize(640, 360);
  window->setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(&context));
  window->setRenderTarget(QQuickRenderTarget::fromOpenGLTexture(target->texture(), target->size()));
  item = new MpvVideoItem(window->contentItem());
  item->setParent(window->contentItem());
  item->setObjectName("video");
  item->setSize(QSizeF(640, 360));
  return control->initialize();
}

bool Scene::frame()
{
  if (!context.makeCurrent(&surface)) return false;
  control->polishItems();
  control->beginFrame();
  control->sync();
  control->render();
  control->endFrame();
  return true;
}

bool Scene::waitFor(const std::function<bool()>& predicate, int ms)
{
  QElapsedTimer deadline;
  deadline.start();
  while (!predicate() && deadline.elapsed() < ms) {
    if (!frame()) return false;
    QTest::qWait(20);
  }
  return predicate();
}

bool Scene::destroyItem()
{
  if (!retired || !context.makeCurrent(&surface)) return false;
  control->invalidate();
  if (!context.makeCurrent(&surface)) return false;
  delete item;
  item = nullptr;
  return true;
}

Scene::~Scene()
{
  if (mayHaveContext && !retired) std::_Exit(EXIT_FAILURE);
  if ((target || item) && !context.makeCurrent(&surface)) std::_Exit(EXIT_FAILURE);
  if (control) control->invalidate();
  if ((target || item) && !context.makeCurrent(&surface)) std::_Exit(EXIT_FAILURE);
  delete item;
  item = nullptr;
  window.reset();
  control.reset();
  target.reset();
  context.doneCurrent();
}

bool propertyFlag(mpv_handle* handle, const char* name)
{
  int value = 0;
  return mpv_get_property(handle, name, MPV_FORMAT_FLAG, &value) >= 0 && value;
}

class Observer
{
public:
  explicit Observer(mpv_handle* core) : client(mpv_create_client(core, "c2-observer"))
  {
    if (client) mpv_observe_property(client, 1, "vo-configured", MPV_FORMAT_FLAG);
  }
  ~Observer() { if (client) mpv_destroy(client); }
  void drain()
  {
    while (client) {
      mpv_event* event = mpv_wait_event(client, 0);
      if (!event || event->event_id == MPV_EVENT_NONE) break;
      if (event->event_id == MPV_EVENT_VIDEO_RECONFIG) ++videoReconfigs;
      if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) ++propertyEvents;
    }
  }
  mpv_handle* client = nullptr;
  int videoReconfigs = 0;
  int propertyEvents = 0;
};

struct CallbackRelease
{
  ~CallbackRelease() { DiagnosticFailureInjection::holdCallback = false; }
};

bool holdNativeCallback(mpv_handle* core, std::thread& release)
{
  DiagnosticFailureInjection::holdCallback = true;
  const char* command[] = {"set", "pause", "yes", nullptr};
  if (mpv_command_async(core, 0, command) < 0) return false;
  QElapsedTimer deadline;
  deadline.start();
  while (!DiagnosticFailureInjection::callbackWaiting.load() && deadline.elapsed() < 5000)
    QTest::qWait(10);
  if (!DiagnosticFailureInjection::callbackWaiting.load()) return false;
  release = std::thread([] {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    DiagnosticFailureInjection::holdCallback = false;
  });
  return true;
}
}

class TestC2RetirementNative : public QObject
{
  Q_OBJECT
private slots:
  void retirement();
private:
  void runScenario(Scene& scene, PlayerComponent& player, const QString& scenario, const QString& media);
  void retireAndVerify(Scene& scene, PlayerComponent& player, const QString& scenario,
                       QSignalSpy& blocked, QSignalSpy& retired, Observer& observer);
};

void TestC2RetirementNative::retirement()
{
  const QString scenario = qEnvironmentVariable("C2_SCENARIO");
  const QString evidence = qEnvironmentVariable("RECOVERY_EVIDENCE_DIR");
  const QString media = qEnvironmentVariable("RECOVERY_MEDIA");
  QVERIFY(scenario == "retire" || scenario == "render-failure" || scenario == "create-failure" || scenario == "stop-hook");
  QVERIFY(QFileInfo(evidence).isDir());
  QVERIFY(QFileInfo(media).isFile());
  Q_INIT_RESOURCE(resources);
  QString error;
  const QString configuredRoot = qEnvironmentVariable("JELLYFIN_TEST_OWNED_ROOT");
  const QString ownedRoot = configuredRoot.isEmpty() ? evidence : configuredRoot;
  const QString root = QFileInfo(ownedRoot).canonicalFilePath();
  const QString path = QFileInfo(evidence).canonicalFilePath();
  QVERIFY(!root.isEmpty() && !path.isEmpty());
  const QString relative = QDir(root).relativeFilePath(path);
  QVERIFY(relative != ".." && !relative.startsWith("../") && !QDir::isAbsolutePath(relative));
  QVERIFY2(MpvTestBootstrap::prepareMpvTestEnvironment(ownedRoot, &error), qPrintable(error));
  QVERIFY2(prepareMpvTestProfile(evidence, &error), qPrintable(error));
  QVERIFY(SettingsComponent::Get().componentInitialize());
  PlayerComponent player;
  QVERIFY(player.componentInitialize());
  Scene scene;
  QVERIFY2(scene.init(), "Could not initialize diagnostic OpenGL scene");
  QVERIFY(!scene.window->isVisible());
  player.setWindow(scene.window.get());
  runScenario(scene, player, scenario, media);
  if (scene.retired) {
    QVERIFY(scene.destroyItem());
    const int closed = DiagnosticFailureInjection::closedOrder;
    const int unregistered = DiagnosticFailureInjection::unregisteredOrder;
    const int freed = DiagnosticFailureInjection::freedOrder;
    const int cleared = DiagnosticFailureInjection::clearedOrder;
    const int ack = DiagnosticFailureInjection::ackOrder;
    const int item = DiagnosticFailureInjection::itemOrder;
    const int core = DiagnosticFailureInjection::coreOrder;
    qInfo() << "C2 retirement order close/unregister/free/clear/ack/item/core:"
            << closed << unregistered << freed << cleared << ack << item << core;
    QVERIFY(closed > 0 && closed < unregistered && unregistered < freed && freed < cleared &&
            cleared < ack && ack < item && item < core);
  }
}

void TestC2RetirementNative::runScenario(Scene& scene, PlayerComponent& player,
                                          const QString& scenario, const QString& media)
{
  QSignalSpy ready(scene.item, &MpvAbstractItem::ready);
  QSignalSpy failed(scene.item, &MpvAbstractItem::renderFailure);
  QSignalSpy retired(scene.item, &MpvAbstractItem::renderRetired);
  QSignalSpy blocked(scene.item, &MpvAbstractItem::renderRetirementBlocked);
  auto* controller = scene.item->controller();
  mpv_handle* core = controller->mpv();
  const quint64 generation = scene.item->renderGeneration();
  if (scenario == "create-failure") DiagnosticFailureInjection::create = MPV_ERROR_UNSUPPORTED;
  scene.mayHaveContext = scenario != "create-failure";
  QVERIFY(scene.waitFor([&] { return !ready.isEmpty() || !failed.isEmpty(); }, 15000));
  if (scenario == "create-failure") {
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.at(0).at(0).toULongLong(), generation);
    QCOMPARE(failed.at(0).at(2).toInt(), int(MPV_ERROR_UNSUPPORTED));
    QCOMPARE(failed.at(0).at(1).value<MpvAbstractItem::RenderFailureStage>(), MpvAbstractItem::RenderFailureStage::Create);
    for (int i = 0; i < 3; ++i) QVERIFY(scene.frame());
    QCOMPARE(failed.count(), 1);
    QVERIFY(retired.isEmpty());
    QCOMPARE(DiagnosticFailureInjection::frees.load(), 0);
    return;
  }
  QVERIFY(!ready.isEmpty());
  QVERIFY(DiagnosticFailureInjection::callbacks.load() > 0);
  QVERIFY(core);
  Observer observer(core);
  QVERIFY(observer.client);
  QVERIFY(player.requestNativeStop().status == PlayerComponent::NativeStopResult::Status::Accepted);
  if (scenario == "render-failure") DiagnosticFailureInjection::render = MPV_ERROR_UNSUPPORTED;
  if (scenario == "stop-hook") player.m_c2HookDelayMs = 3000;
  QVERIFY(player.load(QUrl::fromLocalFile(media).toString(), {{"autoplay", true}}, {{"type", "video"}}));
  if (scenario == "stop-hook") QVERIFY(scene.waitFor([&] { return player.m_c2HooksScheduled > 0; }, 10000));
  else if (scenario == "retire") QVERIFY(scene.waitFor([&] { return player.getPosition() > 500; }, 10000));
  else QVERIFY(scene.waitFor([&] { return !failed.isEmpty(); }, 10000));
  if (scenario == "render-failure") {
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.at(0).at(0).toULongLong(), generation);
    QCOMPARE(failed.at(0).at(2).toInt(), int(MPV_ERROR_UNSUPPORTED));
    QCOMPARE(failed.at(0).at(1).value<MpvAbstractItem::RenderFailureStage>(), MpvAbstractItem::RenderFailureStage::Render);
  }
  retireAndVerify(scene, player, scenario, blocked, retired, observer);
}

void TestC2RetirementNative::retireAndVerify(Scene& scene, PlayerComponent& player,
                                             const QString& scenario, QSignalSpy& blocked,
                                             QSignalSpy& retired, Observer& observer)
{
  auto* controller = scene.item->controller();
  mpv_handle* core = controller->mpv();
  const quint64 generation = scene.item->renderGeneration();
  const auto bindingGeneration = player.m_mpvGeneration;
  const bool ac3 = player.m_doAc3Transcoding;
  const int rejectedBefore = DiagnosticFailureInjection::updatesRejected;
  const int deferredBefore = DiagnosticFailureInjection::updatesDeferred;
  if (scenario == "retire") {
    DiagnosticFailureInjection::deferNextQueuedUpdate = true;
    const int queuedBefore = DiagnosticFailureInjection::updatesQueued;
    QElapsedTimer pending;
    pending.start();
    // Do not pump the GUI queue: retain a real libmpv callback's posted update.
    while (DiagnosticFailureInjection::updatesQueued.load() == queuedBefore && pending.elapsed() < 5000)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    QVERIFY(DiagnosticFailureInjection::updatesQueued.load() > queuedBefore);
  }
  DiagnosticFailureInjection::blockNextRetire = true;
  scene.item->requestRenderRetirement(generation);
  QVERIFY(scene.waitFor([&] { return !blocked.isEmpty(); }, 10000));
  if (scenario == "retire") QVERIFY(DiagnosticFailureInjection::updatesDeferred.load() > deferredBefore);
  QVERIFY(retired.isEmpty());
  QCOMPARE(DiagnosticFailureInjection::frees.load(), 0);
  QCOMPARE(scene.item->controller(), controller);
  QCOMPARE(controller->mpv(), core);
  QCOMPARE(player.m_mpv, controller);
  QCOMPARE(player.m_mpvGeneration, bindingGeneration);
  QCOMPARE(player.m_doAc3Transcoding, ac3);
  if (scenario == "stop-hook") {
    QCOMPARE(player.m_c2HooksCompleted, 0);
    QCOMPARE(player.requestNativeStop().status, PlayerComponent::NativeStopResult::Status::Accepted);
  }
  CallbackRelease release;
  std::thread callbackRelease;
  if (scenario == "retire") QVERIFY(holdNativeCallback(core, callbackRelease));
  scene.item->requestRenderRetirement(generation);
  const bool completed = scene.waitFor([&] { return !retired.isEmpty(); }, 10000);
  if (callbackRelease.joinable()) callbackRelease.join();
  QVERIFY(completed);
  scene.retired = true;
  QCOMPARE(DiagnosticFailureInjection::frees.load(), 1);
  QCOMPARE(retired.count(), 1);
  QCOMPARE(retired.at(0).at(0).toULongLong(), generation);
  if (scenario == "retire")
    QTRY_VERIFY_WITH_TIMEOUT(DiagnosticFailureInjection::updatesRejected.load() > rejectedBefore, 12000);
  QCOMPARE(scene.item->controller(), controller);
  QCOMPARE(controller->mpv(), core);
  QCOMPARE(player.m_mpv, controller);
  QCOMPARE(player.m_mpvGeneration, bindingGeneration);
  QCOMPARE(player.m_doAc3Transcoding, ac3);
  QTest::qWait(150);
  observer.drain();
  qInfo() << "C2 immediately after retirement; Stop already requested:" << (scenario == "stop-hook")
          << "idle-active:" << propertyFlag(core, "idle-active")
          << "vo-configured:" << propertyFlag(core, "vo-configured")
          << "video reconfigs:" << observer.videoReconfigs << "property events:" << observer.propertyEvents;
  if (scenario != "stop-hook")
    QCOMPARE(player.requestNativeStop().status, PlayerComponent::NativeStopResult::Status::Accepted);
  QTest::qWait(3500);
  if (scenario == "stop-hook") QVERIFY(player.m_c2HooksCompleted > 0);
  QVERIFY(propertyFlag(core, "idle-active"));
  observer.drain();
  qInfo() << "C2 actual mpv idle-active:" << propertyFlag(core, "idle-active")
          << "vo-configured:" << propertyFlag(core, "vo-configured")
          << "video reconfigs:" << observer.videoReconfigs << "property events:" << observer.propertyEvents;
  scene.item->requestRenderRetirement(generation);
  QVERIFY(scene.frame());
  QTest::qWait(50);
  QCOMPARE(retired.count(), 1);
}

int main(int argc, char** argv)
{
  QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  QGuiApplication app(argc, argv);
  TestC2RetirementNative test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_c2_retirement_native.moc"
