// C2a diagnostics run one scenario per isolated process.
#include <QtTest/QtTest>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QFile>
#include <QSGRendererInterface>
#include <atomic>
#include <vector>
#include <MpvController>
#include <mpv/client.h>
#include "c2/DiagnosticFailureInjection.h"
#include "c2a_finalizer.h"
#include "c2a_fixture.h"
#include "mpv_test_bootstrap.h"
#include "mpv_test_profile.h"
#include "player/PlayerComponent.h"
#include "settings/SettingsComponent.h"

namespace {
std::atomic<bool> fatalDiagnostic{false};
QtMessageHandler previousHandler = nullptr;
void watchMessage(QtMsgType type, const QMessageLogContext& context, const QString& text)
{
  if (type == QtCriticalMsg || type == QtFatalMsg || text.contains("not being called or stuck", Qt::CaseInsensitive) ||
      text.contains("deadlock", Qt::CaseInsensitive))
    fatalDiagnostic = true;
  if (previousHandler) previousHandler(type, context, text);
}
bool saveCreateEvidence(const QString& evidence, const QJsonObject& observed,
                        const QtThreaded::Audit& paused, const QtThreaded::Audit& settled)
{
  QJsonObject record = observed;
  const QJsonObject audit{{"pause_thread", QString::number(paused.pauseAckThread, 16)},
    {"pause_context", QString::number(paused.pauseAckContext, 16)},
    {"frames_started", qint64(paused.framesStarted)}, {"frames_completed", qint64(paused.framesCompleted)},
    {"worker_joined", settled.workerJoined}, {"context_destroyed", settled.contextDestroyed},
    {"surface_destroyed", settled.surfaceDestroyed}, {"frame_settled", settled.frameSettled},
    {"wrappers_cleared", settled.wrappersCleared}, {"fallback_detached", settled.fallbackDetached},
    {"control_destroyed", settled.controlDestroyed}, {"aliases_cleared", settled.aliasesCleared},
    {"cleanup_count", qint64(settled.cleanupCount)}, {"cleanup_ack_order", qint64(settled.cleanupAckOrder)},
    {"worker_join_order", qint64(settled.workerJoinedOrder)}, {"surface_destroy_order", qint64(settled.surfaceDestroyedOrder)}};
  for (auto it = audit.begin(); it != audit.end(); ++it) record.insert(it.key(), it.value());
  QFile file(evidence + "/c2a-create-failure.json");
  if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
  const QByteArray bytes = QJsonDocument(record).toJson();
  return file.write(bytes) == bytes.size() && file.flush();
}
}

class TestC2RetirementNative : public QObject
{
  Q_OBJECT
private slots:
  void createFailure();
};

void TestC2RetirementNative::createFailure()
{
  const QString root = qEnvironmentVariable("JELLYFIN_TEST_OWNED_ROOT");
  const QString evidence = qEnvironmentVariable("RECOVERY_EVIDENCE_DIR");
  const QString rootPath = QFileInfo(root).canonicalFilePath();
  const QString evidencePath = QFileInfo(evidence).canonicalFilePath();
  QVERIFY(!rootPath.isEmpty() && !evidencePath.isEmpty());
  const QString relative = QDir(rootPath).relativeFilePath(evidencePath);
  QVERIFY(relative != ".." && !relative.startsWith("../") && !QDir::isAbsolutePath(relative));
  QString error;
  QVERIFY2(MpvTestBootstrap::prepareMpvTestEnvironment(root, &error), qPrintable(error));
  QVERIFY2(prepareMpvTestProfile(evidence, &error), qPrintable(error));
  Q_INIT_RESOURCE(resources);
  QVERIFY(SettingsComponent::Get().componentInitialize());
  PlayerComponent player;
  QVERIFY(player.componentInitialize());
  QtThreaded::Scene scene;
  C2aFinalizer cleanup(scene);
  QVERIFY2(scene.initialize(&error), qPrintable(error));
  auto* item = new ProbeItem(scene.contentItem());
  item->setSize(QSizeF(640, 360));
  auto* controller = item->controller();
  QVERIFY(controller && controller->mpv());
  QVERIFY(player.m_mpv == nullptr);
  const quint64 generation = item->renderGeneration();
  QSignalSpy failure(item, &MpvAbstractItem::renderFailure);
  QSignalSpy retired(item, &MpvAbstractItem::renderRetired);
  QSignalSpy ready(item, &MpvAbstractItem::ready);
  DiagnosticFailureInjection::create = MPV_ERROR_UNSUPPORTED;
  cleanup.beginCreate(item);
  QVERIFY2(scene.sync(&error), qPrintable(error));
  QTRY_VERIFY_WITH_TIMEOUT(!failure.isEmpty(), 15000);
  QCOMPARE(failure.count(), 1);
  QCOMPARE(failure.at(0).at(0).toULongLong(), generation);
  QCOMPARE(failure.at(0).at(1).value<MpvAbstractItem::RenderFailureStage>(),
           MpvAbstractItem::RenderFailureStage::Create);
  QCOMPARE(failure.at(0).at(2).toInt(), int(MPV_ERROR_UNSUPPORTED));
  QVERIFY2(scene.pauseFramePump(&error), qPrintable(error));
  const auto paused = scene.audit();
  QVERIFY(paused.framePumpPaused && paused.pauseAcks == 1);
  QVERIFY(paused.pauseAckThread == paused.renderThread && paused.pauseAckThread != paused.guiThread);
  QVERIFY(paused.pauseAckContext != 0 && paused.framesStarted == paused.framesCompleted);
  QCoreApplication::processEvents();
  QCOMPARE(failure.count(), 1);
  QCOMPARE(DiagnosticFailureInjection::create.load(), 0);
  QCOMPARE(DiagnosticFailureInjection::callbacks.load(), 0);
  QVERIFY(ready.isEmpty());
  QVERIFY(player.m_mpv == nullptr);
  QCOMPARE(DiagnosticFailureInjection::frees.load(), 0);
  QCOMPARE(DiagnosticFailureInjection::ackOrder.load(), 0);
  QVERIFY(retired.isEmpty());
  const QJsonObject observed{{"scenario", "create-failure"}, {"generation", qint64(generation)},
    {"failure_count", failure.count()}, {"failure_generation", qint64(failure.at(0).at(0).toULongLong())},
    {"failure_stage", int(failure.at(0).at(1).value<MpvAbstractItem::RenderFailureStage>())},
    {"failure_code", failure.at(0).at(2).toInt()}, {"injection_remaining", DiagnosticFailureInjection::create.load()},
    {"binding_created", player.m_mpv != nullptr}, {"callbacks", DiagnosticFailureInjection::callbacks.load()},
    {"frees", DiagnosticFailureInjection::frees.load()}, {"ack_order", DiagnosticFailureInjection::ackOrder.load()},
    {"ready_count", ready.count()}, {"retired_count", retired.count()}};
  cleanup.confirmNoContext();
  QVERIFY(cleanup.finish());
  const auto settled = scene.audit();
  QCOMPARE(DiagnosticFailureInjection::frees.load(), 0);
  QCOMPARE(DiagnosticFailureInjection::ackOrder.load(), 0);
  QVERIFY(!fatalDiagnostic.load());
  QVERIFY(settled.cleanupAckOrder > 0 && settled.cleanupAckOrder < settled.workerJoinedOrder &&
          settled.workerJoinedOrder < settled.surfaceDestroyedOrder);
  QVERIFY(saveCreateEvidence(evidence, observed, paused, settled));
}

int main(int argc, char** argv)
{
  const QByteArray scenario = qgetenv("C2A_SCENARIO");
  const QHash<QByteArray, QByteArray> scenarios{{"create-failure", "createFailure"},
    {"render-failure", "renderFailure"}, {"active-retirement", "activeRetirement"},
    {"terminal-retirement", "terminalRetirement"}, {"stop-hook", "stopWithPendingHook"}};
  if (!scenarios.contains(scenario)) { qCritical("C2A_SCENARIO must select exactly one known case"); return 2; }
  if (argc != 3 || QByteArray(argv[1]) != "-o") { qCritical("Expected one QTest output argument"); return 2; }
  QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  QGuiApplication app(argc, argv);
  previousHandler = qInstallMessageHandler(watchMessage);
  TestC2RetirementNative test;
  QByteArray selected = scenarios.value(scenario);
  std::vector<char*> arguments(argv, argv + argc);
  arguments.push_back(selected.data());
  const int result = QTest::qExec(&test, int(arguments.size()), arguments.data());
  qInstallMessageHandler(previousHandler);
  return result;
}

#include "test_c2a_retirement_native.moc"
