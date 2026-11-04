#include <QtTest/QtTest>
#include <QGuiApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QThread>
#include <cstdio>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include "color_item.h"
#include "helper.h"

class TestThreadedRender : public QObject
{
  Q_OBJECT
private slots:
  void init();
  void independentFrames();
  void initializationFailures_data();
  void initializationFailures();
  void pendingReadbackCancelled();
  void blockedCleanupIsFailure();
};

void TestThreadedRender::init()
{
  QTest::failOnWarning(QRegularExpression(
    ".*(thread affinity|different thread|wrong thread|makeCurrent|current context|QObject::|QOpenGLContext).*",
    QRegularExpression::CaseInsensitiveOption));
}

static bool differentBrightFrames(const QList<QtThreaded::Frame>& frames, quint64 baseline,
                                  qint64 startNs, qint64 endNs)
{
  for (qsizetype i = 0; i < frames.size(); ++i) {
    if (frames[i].sequence <= baseline || frames[i].monotonicNs <= startNs ||
        frames[i].monotonicNs >= endNs || frames[i].image.isNull()) continue;
    const QColor a = frames[i].image.pixelColor(frames[i].image.width() / 2, frames[i].image.height() / 2);
    if (a.red() + a.green() + a.blue() < 150) continue;
    for (qsizetype j = i + 1; j < frames.size(); ++j) {
      if (frames[j].sequence <= baseline || frames[j].monotonicNs <= startNs ||
          frames[j].monotonicNs >= endNs || frames[j].image.isNull() ||
          frames[j].sequence == frames[i].sequence) continue;
      const QColor b = frames[j].image.pixelColor(frames[j].image.width() / 2, frames[j].image.height() / 2);
      if (b.red() + b.green() + b.blue() >= 150 && a != b) return true;
    }
  }
  return false;
}

void TestThreadedRender::independentFrames()
{
  QtThreaded::Scene scene;
  QString error;
  QVERIFY(!scene.sync(&error));
  QVERIFY2(scene.initialize(&error), qPrintable(error));
  auto* item = new ColorItem(scene.contentItem());
  item->setSize(QSizeF(640, 360));
  const auto observation = item->observation();
  QVERIFY2(scene.sync(&error), qPrintable(error));
  QVERIFY(scene.requestReadback());
  QTRY_COMPARE_WITH_TIMEOUT(scene.audit().readbacksCompleted, quint64(1), 10000);
  QCOMPARE(scene.audit().readbacksAccepted, quint64(1));
  QCOMPARE(scene.audit().readbacksCancelled, quint64(0));
  QTRY_VERIFY_WITH_TIMEOUT(observation->calls.load() >= 2, 10000);
  const auto before = observation->calls.load();
  const auto priorFrames = scene.frames();
  quint64 baseline = 0;
  for (const auto& frame : priorFrames) baseline = qMax(baseline, frame.sequence);
  const auto startNs = QtThreaded::monotonicNanos();
  QThread::msleep(350); // Deliberately withhold GUI event processing and synchronization.
  const auto endNs = QtThreaded::monotonicNanos();
  const auto after = observation->calls.load();
  QVERIFY(after >= before + 2);
  const auto frames = scene.frames();
  QVERIFY(differentBrightFrames(frames, baseline, startNs, endNs));
  const auto audit = scene.audit();
  QVERIFY(audit.guiThread != 0 && audit.renderThread != 0);
  QVERIFY(audit.guiThread != audit.renderThread);
  QCOMPARE(audit.initThread, audit.renderThread);
  QCOMPARE(audit.renderThreadObserved, audit.renderThread);
  QCOMPARE(observation->renderThread.load(), audit.renderThread);
  QVERIFY(observation->context.load() != 0);
  QVERIFY(!observation->wrongContext.load());
  QVERIFY(!observation->wrongThread.load());
  QVERIFY2(scene.shutdown(&error), qPrintable(error));
  QVERIFY2(scene.shutdown(&error), qPrintable(error));
  const auto end = scene.audit();
  QVERIFY(end.workerJoined && end.contextDestroyed && end.surfaceDestroyed && end.controlDestroyed);
  QVERIFY(end.admissionClosed && end.frameSettled);
  QVERIFY(end.affinitiesVerified && end.noActiveFrameAtCleanup);
  QCOMPARE(end.framesStarted, end.framesCompleted);
  QVERIFY(end.contextCurrentAtCleanup && end.wrappersCleared && end.aliasesCleared && end.fallbackDetached);
  QCOMPARE(end.rhiCreatedCount, quint64(1));
  QCOMPARE(end.rhiDestroyedCount, quint64(1));
  QCOMPARE(end.cleanupCount, quint64(1));
  QCOMPARE(end.controlDestroyedCount, quint64(1));
  QCOMPARE(end.contextDestroyedCount, quint64(1));
  QCOMPARE(end.surfaceDestroyedCount, quint64(1));
  QCOMPARE(observation->destructorThread.load(), audit.renderThread);
  QCOMPARE(end.cleanupThread, audit.renderThread);
  QCOMPARE(end.rhiCleanupThread, audit.renderThread);
  QCOMPARE(end.contextDestructionThread, audit.renderThread);
  QCOMPARE(end.surfaceDestructionThread, audit.guiThread);
  const auto finalCalls = observation->calls.load();
  QCoreApplication::processEvents();
  QCOMPARE(observation->calls.load(), finalCalls);
  QVERIFY(!scene.sync(&error));
}

void TestThreadedRender::initializationFailures_data()
{
  QTest::addColumn<int>("failure");
  QTest::newRow("wrong-version") << static_cast<int>(QtThreaded::FailurePoint::WrongVersion);
  QTest::newRow("before-transfer") << static_cast<int>(QtThreaded::FailurePoint::BeforeTransfer);
  QTest::newRow("after-transfer") << static_cast<int>(QtThreaded::FailurePoint::AfterTransfer);
  QTest::newRow("after-rhi") << static_cast<int>(QtThreaded::FailurePoint::AfterRhiCreated);
  QTest::newRow("after-init") << static_cast<int>(QtThreaded::FailurePoint::AfterInit);
}

void TestThreadedRender::initializationFailures()
{
  QFETCH(int, failure);
  QtThreaded::Scene scene(static_cast<QtThreaded::FailurePoint>(failure));
  QString error;
  QVERIFY(!scene.initialize(&error));
  QVERIFY(!error.isEmpty());
  QVERIFY2(scene.shutdown(&error), qPrintable(error));
  QVERIFY2(scene.shutdown(&error), qPrintable(error));
  const auto audit = scene.audit();
  if (failure == static_cast<int>(QtThreaded::FailurePoint::WrongVersion)) {
    QVERIFY(!audit.workerReady && !audit.transferred && !audit.rhiCreated);
    QVERIFY(!audit.privateBoundaryEntered);
    QVERIFY(!audit.workerJoined && !audit.surfaceDestroyed && !audit.contextDestroyed);
    QCOMPARE(audit.rhiCreatedCount, quint64(0));
    QCOMPARE(audit.cleanupCount, quint64(0));
    QCOMPARE(audit.controlDestroyedCount, quint64(0));
    QCOMPARE(audit.contextDestroyedCount, quint64(0));
    QCOMPARE(audit.surfaceDestroyedCount, quint64(0));
    return;
  }
  if (failure == static_cast<int>(QtThreaded::FailurePoint::BeforeTransfer)) {
    QVERIFY(audit.workerReady && audit.workerJoined && !audit.transferred && !audit.rhiCreated);
    QVERIFY(audit.contextDestroyed && audit.surfaceDestroyed && audit.controlDestroyed);
    QCOMPARE(audit.contextDestroyedCount, quint64(1));
    QCOMPARE(audit.surfaceDestroyedCount, quint64(1));
    QCOMPARE(audit.controlDestroyedCount, quint64(1));
    QCOMPARE(audit.contextDestructionThread, audit.guiThread);
    QCOMPARE(audit.surfaceDestructionThread, audit.guiThread);
    QCOMPARE(audit.rhiCreatedCount, quint64(0));
    QCOMPARE(audit.cleanupCount, quint64(0));
    return;
  }
  QVERIFY(audit.workerJoined && audit.surfaceDestroyed);
  QVERIFY(audit.renderThread != 0 && audit.renderThread != audit.guiThread);
  QCOMPARE(audit.initThread, audit.renderThread);
  if (failure == static_cast<int>(QtThreaded::FailurePoint::AfterRhiCreated)) {
    QVERIFY(audit.rhiCreated);
    QVERIFY(audit.partialRhiVerified);
    QCOMPARE(audit.rhiCreatedCount, quint64(1));
    QCOMPARE(audit.rhiDestroyedCount, quint64(1));
    QCOMPARE(audit.rhiCleanupThread, audit.renderThread);
    QVERIFY(!audit.initialized && !audit.targetReady);
    QCOMPARE(audit.frames, quint64(0));
  }
  if (failure == static_cast<int>(QtThreaded::FailurePoint::AfterInit)) {
    QVERIFY(audit.initialized);
    QCOMPARE(audit.frames, quint64(0));
  }
}

void TestThreadedRender::pendingReadbackCancelled()
{
  QtThreaded::Scene scene(QtThreaded::FailurePoint::HoldReadback);
  QString error;
  QVERIFY2(scene.initialize(&error), qPrintable(error));
  auto* item = new ColorItem(scene.contentItem());
  item->setSize(QSizeF(128, 128));
  QVERIFY2(scene.sync(&error), qPrintable(error));
  QVERIFY(scene.requestReadback());
  const auto pending = scene.audit();
  QCOMPARE(pending.readbacksAccepted, quint64(1));
  QCOMPARE(pending.readbacksCompleted, quint64(0));
  QCOMPARE(pending.readbacksCancelled, quint64(0));
  QVERIFY2(scene.shutdown(&error), qPrintable(error));
  QVERIFY2(scene.shutdown(&error), qPrintable(error));
  const auto settled = scene.audit();
  QCOMPARE(settled.readbacksAccepted, quint64(1));
  QCOMPARE(settled.readbacksCompleted, quint64(0));
  QCOMPARE(settled.readbacksCancelled, quint64(1));
  QCOMPARE(settled.cleanupCount, quint64(1));
  QVERIFY(!scene.requestReadback());
  const auto rejected = scene.audit();
  QCOMPARE(rejected.readbacksRejected, settled.readbacksRejected + 1);
  QCOMPARE(rejected.readbacksAccepted, quint64(1));
  QCOMPARE(rejected.readbacksCancelled, quint64(1));
}

void TestThreadedRender::blockedCleanupIsFailure()
{
  QProcess child;
#ifdef Q_OS_WIN
  child.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
    args->flags |= CREATE_NO_WINDOW;
  });
#endif
  child.setProcessChannelMode(QProcess::MergedChannels);
  child.start(QCoreApplication::applicationFilePath(), {"--blocked-cleanup-child"});
  QVERIFY(child.waitForStarted(5000));
  const bool finished = child.waitForFinished(15000);
  if (!finished) {
    child.kill();
    child.waitForFinished(5000);
  }
  QVERIFY2(finished, "Blocked-cleanup child did not exit; the owned child was terminated");
  const auto output = child.readAll();
  QVERIFY(output.contains("BLOCKED_CLEANUP_OBSERVED"));
  QVERIFY(child.exitStatus() != QProcess::NormalExit || child.exitCode() != 0);
}

int main(int argc, char** argv)
{
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  QGuiApplication app(argc, argv);
  if (app.arguments().contains("--blocked-cleanup-child")) {
    QtThreaded::Scene scene(QtThreaded::FailurePoint::BlockCleanup);
    QString error;
    if (!scene.initialize(&error)) return 3;
    if (scene.shutdown(&error)) return 4;
    const auto audit = scene.audit();
    if (audit.contextDestroyed || audit.workerJoined) return 5;
    fprintf(stderr, "BLOCKED_CLEANUP_OBSERVED\n");
    fflush(stderr);
    return 0; // Scene destructor must report unsuccessful containment.
  }
  TestThreadedRender test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_threaded_render.moc"
