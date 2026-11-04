#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <MpvController>
#include <atomic>
#include <chrono>
#include <thread>
#include "mpv_thread_cleanup.h"
#include "mpv_test_bootstrap.h"
#include "mpv_test_profile.h"
#include "settings/SettingsComponent.h"

namespace {
std::atomic<bool> renderWarning{false};
QtMessageHandler previousHandler = nullptr;
void watchMessages(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
  if (message.contains("not being called or stuck", Qt::CaseInsensitive) ||
      message.contains("deadlock", Qt::CaseInsensitive)) renderWarning = true;
  if (previousHandler) previousHandler(type, context, message);
}
struct MessageWatch {
  MessageWatch() { renderWarning = false; previousHandler = qInstallMessageHandler(watchMessages); }
  ~MessageWatch() { qInstallMessageHandler(previousHandler); }
};
int brightPixels(const QImage& image)
{
  int count = 0;
  for (int y = 0; y < image.height(); y += 4)
    for (int x = 0; x < image.width(); x += 4) {
      const auto pixel = image.pixelColor(x, y);
      if (pixel.red() + pixel.green() + pixel.blue() > 300) ++count;
    }
  return count;
}
bool movingFrames(const QList<QtThreaded::Frame>& frames)
{
  for (const auto& first : frames)
    for (const auto& second : frames)
      if (first.sequence < second.sequence && brightPixels(first.image) > 100 &&
          brightPixels(second.image) > 100 && first.image != second.image) return true;
  return false;
}
}

class TestMpvThreadIndependence : public QObject
{
  Q_OBJECT
private slots:
  void healthyPlayback();
private:
  bool prepare(const QString& evidence, const QString& media);
  void exercise(QtThreaded::Scene& scene, PlayerComponent& player, ProbeItem* item,
                const QString& media, const QString& evidence);
  void verifyHold(QtThreaded::Scene& scene, PlayerComponent& player, ProbeItem* item,
                  const QString& evidence);
};

bool TestMpvThreadIndependence::prepare(const QString& evidence, const QString& media)
{
  const QString root = qEnvironmentVariable("JELLYFIN_TEST_OWNED_ROOT");
  const QString rootPath = QFileInfo(root).canonicalFilePath();
  const QString evidencePath = QFileInfo(evidence).canonicalFilePath();
  if (rootPath.isEmpty() || evidencePath.isEmpty() || !QFileInfo(media).isFile()) return false;
  const QString relative = QDir(rootPath).relativeFilePath(evidencePath);
  if (relative == ".." || relative.startsWith("../") || QDir::isAbsolutePath(relative)) return false;
  QString error;
  if (!MpvTestBootstrap::prepareMpvTestEnvironment(root, &error) ||
      !prepareMpvTestProfile(evidence, &error)) { qCritical() << error; return false; }
  Q_INIT_RESOURCE(resources);
  return SettingsComponent::Get().componentInitialize();
}

void TestMpvThreadIndependence::healthyPlayback()
{
  MessageWatch watch;
  PlayerComponent player;
  QSignalSpy failed(&player, &PlayerComponent::error);
  QSignalSpy ended(&player, &PlayerComponent::finished);
  QSignalSpy canceled(&player, &PlayerComponent::canceled);
  QtThreaded::Scene scene;
  MpvThreadCleanup cleanup(player, scene);
  cleanup.setChecks([&] { return failed.isEmpty() && ended.isEmpty() && canceled.isEmpty() && !renderWarning.load(); },
                    [&] { return failed.isEmpty() && ended.isEmpty() && !renderWarning.load(); });
  const QString evidence = qEnvironmentVariable("RECOVERY_EVIDENCE_DIR");
  const QString media = qEnvironmentVariable("RECOVERY_MEDIA");
  QVERIFY(prepare(evidence, media));
  QVERIFY(player.componentInitialize());
  QString error;
  QVERIFY2(scene.initialize(&error), qPrintable(error));
  auto* item = new ProbeItem(scene.contentItem());
  item->setObjectName("video");
  item->setSize(QSizeF(640, 360));
  auto* controller = item->controller();
  mpv_handle* core = controller ? controller->mpv() : nullptr;
  player.setWindow(scene.contentItem()->window());
  const quint64 binding = player.m_mpvGeneration;
  cleanup.attach(item, core, [&, item, controller, core, binding] { return item->controller() == controller && controller &&
    controller->mpv() == core && player.m_mpv == controller && player.m_mpvGeneration == binding; });
  QVERIFY(core && player.m_mpv == controller);
  exercise(scene, player, item, media, evidence);
  QCoreApplication::processEvents();
  QVERIFY(failed.isEmpty() && ended.isEmpty() && canceled.isEmpty() && !renderWarning.load());
  const bool cleaned = cleanup.finish();
  QCoreApplication::processEvents();
  const auto audit = scene.audit();
  const int ackOrder = DiagnosticFailureInjection::ackOrder.load();
  const int itemOrder = DiagnosticFailureInjection::itemOrder.load();
  const int coreOrder = DiagnosticFailureInjection::coreOrder.load();
  QJsonObject lifecycleData = cleanup.evidence();
  lifecycleData.insert("ack_order", ackOrder);
  lifecycleData.insert("item_order", itemOrder);
  lifecycleData.insert("core_order", coreOrder);
  lifecycleData.insert("frees", DiagnosticFailureInjection::frees.load());
  lifecycleData.insert("binding_released", player.m_mpv == nullptr);
  lifecycleData.insert("worker_joined", audit.workerJoined);
  lifecycleData.insert("cleanup_count", qint64(audit.cleanupCount));
  lifecycleData.insert("context_destroyed", audit.contextDestroyed);
  lifecycleData.insert("surface_destroyed", audit.surfaceDestroyed);
  lifecycleData.insert("cleanup_ack_order", qint64(audit.cleanupAckOrder));
  lifecycleData.insert("worker_join_order", qint64(audit.workerJoinedOrder));
  lifecycleData.insert("surface_destroy_order", qint64(audit.surfaceDestroyedOrder));
  QFile lifecycle(evidence + "/mpv-lifecycle.json");
  QVERIFY(lifecycle.open(QIODevice::WriteOnly | QIODevice::NewOnly));
  QVERIFY(lifecycle.write(QJsonDocument(lifecycleData).toJson()) > 0);
  QVERIFY2(cleaned, "Stop, idle, retirement, or worker cleanup failed");
  QVERIFY(failed.isEmpty() && ended.isEmpty() && !renderWarning.load());
  QVERIFY(player.m_mpv == nullptr);
  QCOMPARE(DiagnosticFailureInjection::frees.load(), 1);
  QVERIFY(ackOrder > 0 && ackOrder < itemOrder && itemOrder < coreOrder);
  QVERIFY(audit.workerJoined && audit.cleanupCount == 1 && audit.contextDestroyed && audit.surfaceDestroyed);
  QVERIFY(audit.cleanupAckOrder > 0 && audit.cleanupAckOrder < audit.workerJoinedOrder &&
          audit.workerJoinedOrder < audit.surfaceDestroyedOrder);
}

void TestMpvThreadIndependence::exercise(QtThreaded::Scene& scene, PlayerComponent& player,
                                         ProbeItem* item, const QString& media, const QString& evidence)
{
  QSignalSpy ready(item, &MpvAbstractItem::ready);
  QString error;
  QVERIFY2(scene.sync(&error), qPrintable(error));
  QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty(), 15000);
  QVERIFY(player.load(QUrl::fromLocalFile(media).toString(), {{"autoplay", true}}, {{"type", "video"}}));
  QTRY_VERIFY_WITH_TIMEOUT(player.getPosition() > 500, 10000);
  QTRY_VERIFY_WITH_TIMEOUT(movingFrames(scene.frames()), 10000);
  verifyHold(scene, player, item, evidence);
}

void TestMpvThreadIndependence::verifyHold(QtThreaded::Scene& scene, PlayerComponent& player,
                                           ProbeItem* item, const QString& evidence)
{
  quint64 baseline = 0;
  for (const auto& frame : scene.frames()) baseline = qMax(baseline, frame.sequence);
  const auto initial = item->observed->snapshot();
  QVERIFY(!initial.isEmpty());
  const auto original = initial.last();
  const auto audit = scene.audit();
  QVERIFY(original.context && original.renderer && original.thread == audit.renderThread);
  const qint64 start = QtThreaded::monotonicNanos();
  qint64 firstPosition = -1, lastPosition = -1;
  QJsonArray getters;
  while (QtThreaded::monotonicNanos() - start < 350000000) {
    const qint64 entered = QtThreaded::monotonicNanos();
    const qint64 position = player.getPosition();
    const qint64 returned = QtThreaded::monotonicNanos();
    qInfo() << "GUI getter entry/return/position" << entered << returned << position;
    getters.append(QJsonObject{{"entered", entered}, {"returned", returned}, {"position", position}});
    if (firstPosition < 0) firstPosition = position;
    lastPosition = position;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  const qint64 end = QtThreaded::monotonicNanos();
  QVERIFY(getters.size() >= 2);
  QVERIFY(lastPosition > firstPosition);
  QList<QtThreaded::Frame> fresh;
  for (const auto& frame : scene.frames())
    if (frame.sequence > baseline && frame.monotonicNs > start && frame.monotonicNs < end &&
        brightPixels(frame.image) > 100) fresh.append(frame);
  QVERIFY(fresh.size() >= 2);
  int different = 1;
  while (different < fresh.size() && fresh[different].image == fresh[0].image) ++different;
  QVERIFY(different < fresh.size());
  QVERIFY(fresh[0].image.save(evidence + "/mpv-hold-1.png"));
  QVERIFY(fresh[different].image.save(evidence + "/mpv-hold-2.png"));
  QJsonArray renders;
  for (const auto& event : item->observed->snapshot()) {
    if (event.entered <= start || event.returned >= end || event.returned <= event.entered) continue;
    QVERIFY(event.thread == audit.renderThread && event.thread != audit.guiThread);
    QVERIFY(event.context && event.renderer);
    QCOMPARE(event.context, original.context);
    QCOMPARE(event.renderer, original.renderer);
    renders.append(QJsonObject{{"entered", event.entered}, {"returned", event.returned},
      {"thread", QString::number(event.thread, 16)}, {"context", QString::number(event.context, 16)},
      {"renderer", QString::number(event.renderer, 16)}});
  }
  QVERIFY(renders.size() >= 2);
  const QJsonObject result{{"probe", "fixture MpvRenderer subclass entry and return"},
    {"start", start}, {"end", end}, {"getters", getters}, {"wrapper_renders", renders},
    {"frame1_sequence", qint64(fresh[0].sequence)}, {"frame1_time", fresh[0].monotonicNs},
    {"frame1_thread", QString::number(fresh[0].renderThread, 16)},
    {"frame2_sequence", qint64(fresh[different].sequence)}, {"frame2_time", fresh[different].monotonicNs},
    {"frame2_thread", QString::number(fresh[different].renderThread, 16)}};
  QFile file(evidence + "/mpv-hold.json");
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
  QVERIFY(file.write(QJsonDocument(result).toJson()) > 0);
}

int main(int argc, char** argv)
{
  QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  QGuiApplication app(argc, argv);
  TestMpvThreadIndependence test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_mpv_thread_independence.moc"
