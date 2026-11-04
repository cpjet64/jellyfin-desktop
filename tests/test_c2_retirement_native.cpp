// Diagnostic-only fixture: the test retains the item, core and GL context
// until retirement ACK. Run each C2_SCENARIO in a separate contained process.
#include <QtTest/QtTest>
#include <QDir>
#include <QColor>
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
#include "c2_event_observer.h"
#include "c2_replay_recipe.h"
#include "c2_scene.h"

namespace {
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
  void recovery();
private:
  void runScenario(Scene& scene, PlayerComponent& player, const QString& scenario, const QString& media);
  void retireAndVerify(Scene& scene, PlayerComponent& player, const QString& scenario,
                       QSignalSpy& blocked, QSignalSpy& retired, C2EventObserver& observer);
  void recoverScenario(Scene& scene, PlayerComponent& player, const QString& scenario,
                       const QString& media, const QString& evidence);
  void recreate(Scene& scene, PlayerComponent& player, C2ReplayRecipe& recipe, C2EventObserver& observer,
                const QString& scenario, const QString& evidence);
  void verifyRecovery(Scene& scene, PlayerComponent& player, C2ReplayRecipe& recipe,
                      const QString& scenario, const QString& evidence);
  void holdStaleCallback();
  void verifyStaleCallback(Scene& scene, PlayerComponent& player, MpvController* controller,
                           quint64 binding, int destroyedBefore, int rejectedBefore, QSignalSpy& retired);
  void verifyStopped(Scene& scene, C2ReplayRecipe& recipe, C2EventObserver& observer,
                     mpv_handle* core, bool previousAttempt);
  void verifyFailedCreate(Scene& scene, quint64 first, QSignalSpy& failed,
                          QSignalSpy& noContext, QSignalSpy& created, QSignalSpy& retired);
  void finishRecovery(Scene& scene, PlayerComponent& player, C2ReplayRecipe& recipe,
                      C2EventObserver& observer, const QString& scenario, const QString& evidence,
                      QSignalSpy& failed, QSignalSpy& retired);
};

#include "c2_recovery_cases.h"

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
  C2EventObserver observer(core);
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
                                             QSignalSpy& retired, C2EventObserver& observer)
{
  auto* controller = scene.item->controller();
  mpv_handle* core = controller->mpv();
  const quint64 generation = scene.item->renderGeneration();
  const auto bindingGeneration = player.m_mpvGeneration;
  const bool ac3 = player.m_doAc3Transcoding;
  const int rejectedBefore = DiagnosticFailureInjection::deferredUpdatesRejected;
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
  if (scenario == "retire") {
    QTest::qWait(250); // Pump GUI events without running a render-control frame.
    QVERIFY(blocked.isEmpty());
    QVERIFY(retired.isEmpty());
    QCOMPARE(DiagnosticFailureInjection::frees.load(), 0);
    QCOMPARE(scene.item->controller(), controller);
    QCOMPARE(controller->mpv(), core);
    qInfo() << "C2 retirement job made no progress while frames were withheld";
  }
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
    QTRY_VERIFY_WITH_TIMEOUT(DiagnosticFailureInjection::deferredUpdatesRejected.load() > rejectedBefore, 12000);
  QCOMPARE(scene.item->controller(), controller);
  QCOMPARE(controller->mpv(), core);
  QCOMPARE(player.m_mpv, controller);
  QCOMPARE(player.m_mpvGeneration, bindingGeneration);
  QCOMPARE(player.m_doAc3Transcoding, ac3);
  QTest::qWait(150);
  observer.drain();
  const auto idleAfterRetire = observer.flag("idle-active");
  const auto videoAfterRetire = observer.flag("vo-configured");
  QCOMPARE(idleAfterRetire.error, 0);
  QCOMPARE(videoAfterRetire.error, 0);
  qInfo() << "C2 immediately after retirement; Stop already requested:" << (scenario == "stop-hook")
          << "idle-active:" << idleAfterRetire.value << "vo-configured:" << videoAfterRetire.value
          << "observed events:" << observer.events.size();
  if (scenario != "stop-hook")
    QCOMPARE(player.requestNativeStop().status, PlayerComponent::NativeStopResult::Status::Accepted);
  QTest::qWait(3500);
  if (scenario == "stop-hook") {
    QVERIFY(player.m_c2HooksCompleted > 0);
    QCOMPARE(player.m_c2HookContinueCalls, 1);
    QCOMPARE(player.m_c2LastScheduledHookId, player.m_c2LastContinuedHookId);
    QCOMPARE(player.m_c2LastHookContinueResult, 0);
  }
  const auto idleAfterStop = observer.flag("idle-active");
  QCOMPARE(idleAfterStop.error, 0);
  QVERIFY(idleAfterStop.value);
  observer.drain();
  const auto videoAfterStop = observer.flag("vo-configured");
  QCOMPARE(videoAfterStop.error, 0);
  qInfo() << "C2 actual mpv idle-active:" << idleAfterStop.value
          << "vo-configured:" << videoAfterStop.value << "observed events:" << observer.events.size();
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
  QVector<QByteArray> arguments;
  for (int i = 0; i < argc; ++i) arguments.append(argv[i]);
  arguments.append(qEnvironmentVariableIsEmpty("C2_RECOVERY_SCENARIO") ? "retirement" : "recovery");
  QVector<char*> pointers;
  for (auto& argument : arguments) pointers.append(argument.data());
  return QTest::qExec(&test, pointers.size(), pointers.data());
}

#include "test_c2_retirement_native.moc"
