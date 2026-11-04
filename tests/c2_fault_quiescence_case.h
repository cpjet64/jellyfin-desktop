#pragma once

#include <QScopeGuard>
#include "c2a_fixture.h"
#include "c2a_hold_gate.h"
#include "c2_fault_quiescence_policy.h"

namespace {
struct C2QuiescenceState
{
  int stopStatus = -1;
  int stopError = 0;
  int forceStatus = -1;
  qint64 stopEntered = 0;
  qint64 stopReturned = 0;
  qint64 terminalNs = 0;
  qint64 closeAckNs = 0;
  int matchingFailures = 0;
};

bool saveQuiescence(const QString& path, const QJsonObject& record)
{
  QFile file(path + "/c2-fault-quiescence.json");
  if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
  const auto bytes = QJsonDocument(record).toJson();
  return file.write(bytes) == bytes.size() && file.flush();
}

bool saveQuiescenceFrame(const QString& path, const QImage& image)
{
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && image.save(&file, "PNG") && file.flush();
}

QJsonArray quiescenceEvents(const C2EventObserver& observer)
{
  QJsonArray result;
  for (const auto& event : observer.events)
    result.append(QJsonObject{{"kind", int(event.kind)}, {"entry", qint64(event.playlistId)},
      {"reason", event.reason}, {"error", event.error}, {"insert_id", qint64(event.insertId)},
      {"insert_count", event.insertCount}});
  return result;
}

bool observedSkip(const C2aFixture& fixture, const MpvRenderEvent& render)
{
  using D = DiagnosticFailureInjection;
  return D::servicePendingCount.load() > 0 && D::serviceSkipCount.load() > 0 &&
    D::ordinaryPostFaultCount.load() == 0 && D::serviceResult.load() == 0 &&
    D::serviceGeneration.load() == fixture.generation && D::serviceThread.load() == render.thread &&
    D::serviceContext.load() == render.context && D::servicePendingNs.load() > 0 &&
    D::serviceSkipEnteredNs.load() >= D::servicePendingNs.load() &&
    D::serviceSkipReturnedNs.load() >= D::serviceSkipEnteredNs.load();
}

QJsonObject quiescenceServiceEvidence()
{
  using D = DiagnosticFailureInjection;
  return {{"pending_count", D::servicePendingCount.load()}, {"skip_count", D::serviceSkipCount.load()},
    {"ordinary_post_fault", D::ordinaryPostFaultCount.load()}, {"service_generation", qint64(D::serviceGeneration.load())},
    {"service_thread", QString::number(D::serviceThread.load(), 16)},
    {"service_context", QString::number(D::serviceContext.load(), 16)}, {"service_result", D::serviceResult.load()},
    {"pending_ns", qint64(D::servicePendingNs.load())}, {"skip_entered_ns", qint64(D::serviceSkipEnteredNs.load())},
    {"skip_returned_ns", qint64(D::serviceSkipReturnedNs.load())}, {"service_close_ns", qint64(D::serviceCloseNs.load())},
    {"service_close_order", D::serviceCloseOrder.load()}, {"free_entered_ns", qint64(D::freeEnteredNs.load())}};
}

bool quiescenceProperties(mpv_handle* core, QJsonObject& record)
{
  int idle = 0;
  char* vo = nullptr;
  char* force = nullptr;
  const int idleStatus = mpv_get_property(core, "idle-active", MPV_FORMAT_FLAG, &idle);
  const int voStatus = mpv_get_property(core, "current-vo", MPV_FORMAT_STRING, &vo);
  const int forceStatus = mpv_get_property(core, "force-window", MPV_FORMAT_STRING, &force);
  const bool forceNo = force && QByteArray(force) == "no";
  if (vo) mpv_free(vo);
  if (force) mpv_free(force);
  record = {{"idle_status", idleStatus}, {"idle", idle}, {"vo_status", voStatus},
            {"force_status", forceStatus}, {"force_no", forceNo}};
  return c2ErrorTerminalProperties(idleStatus, idle, voStatus, forceStatus, forceNo);
}

bool finishQuiescence(C2aFixture& fixture, C2EventObserver& observer, QSignalSpy& retired,
                      const std::function<bool()>& sameBinding, const C2QuiescenceState& state,
                      const QJsonObject& record, const QString& evidence)
{
  if (!fixture.cleanup.requestRetirement(fixture.generation, sameBinding) || retired.count() != 1 ||
      DiagnosticFailureInjection::freeEnteredNs.load() < state.closeAckNs) return false;
  observer.close();
  if (!fixture.cleanup.finish() || fatalDiagnostic.load()) return false;
  QJsonObject complete = record;
  complete.insert("service", quiescenceServiceEvidence());
  complete.insert("retired", retired.count());
  complete.insert("frees", DiagnosticFailureInjection::frees.load());
  complete.insert("free_order", DiagnosticFailureInjection::freedOrder.load());
  complete.insert("clear_order", DiagnosticFailureInjection::clearedOrder.load());
  complete.insert("ack_order", DiagnosticFailureInjection::ackOrder.load());
  const auto audit = fixture.scene.audit();
  complete.insert("cleanup_ack_order", qint64(audit.cleanupAckOrder));
  complete.insert("worker_join_order", qint64(audit.workerJoinedOrder));
  complete.insert("surface_destroy_order", qint64(audit.surfaceDestroyedOrder));
  return saveQuiescence(evidence, complete);
}

QJsonObject quiescenceRecord(const C2aFixture& fixture, const C2EventObserver& observer,
                             int64_t entry, quint64 binding, const MpvRenderEvent& render,
                             const C2QuiescenceState& state, const QJsonObject& properties,
                             int canceled, int closed)
{
  return {{"entry", qint64(entry)}, {"events", quiescenceEvents(observer)},
    {"generation", qint64(fixture.generation)}, {"binding", qint64(binding)},
    {"core", QString::number(reinterpret_cast<quintptr>(fixture.core), 16)},
    {"controller", QString::number(reinterpret_cast<quintptr>(fixture.controller), 16)},
    {"render_thread", QString::number(render.thread, 16)}, {"render_context", QString::number(render.context, 16)},
    {"stop_entered_ns", state.stopEntered}, {"stop_returned_ns", state.stopReturned},
    {"stop_status", state.stopStatus}, {"stop_error", state.stopError}, {"force_status", state.forceStatus},
    {"terminal_confirmed_ns", state.terminalNs}, {"service_close_ack_ns", state.closeAckNs},
    {"terminal_properties", properties}, {"first_frame_sequence", qint64(fixture.playbackFirstFrame.sequence)},
    {"second_frame_sequence", qint64(fixture.playbackSecondFrame.sequence)},
    {"playback_start_ms", fixture.playbackStartPosition}, {"playback_end_ms", fixture.playbackEndPosition},
    {"canceled", canceled}, {"service_closed", closed}};
}

void runQuiescence(PlayerComponent& player, C2aFixture& fixture, C2EventObserver& observer,
                   int64_t entry, quint64 binding, const MpvRenderEvent& render,
                   const std::function<bool()>& sameBinding, const QString& evidence)
{
  QSignalSpy canceled(&player, &PlayerComponent::canceled);
  QSignalSpy closed(fixture.item, &MpvAbstractItem::syntheticFrameServiceClosed);
  QSignalSpy closeFailed(fixture.item, &MpvAbstractItem::syntheticFrameServiceCloseFailed);
  QSignalSpy retired(fixture.item, &MpvAbstractItem::renderRetired);
  std::atomic<bool> held{false}, done{false};
  C2aHoldGate deadline(held, [&] { return done.load(); }, [](auto) {}, [](auto outcome) {
    if (outcome == C2aHoldGate::Outcome::Timeout) std::_Exit(EXIT_FAILURE);
  });
  deadline.start(std::chrono::seconds(15));
  QTRY_VERIFY_WITH_TIMEOUT(deadline.ready.load(), 5000);
  QVERIFY(deadline.arm());
  C2QuiescenceState state;
  const auto closeConnection = QObject::connect(fixture.item, &MpvAbstractItem::syntheticFrameServiceClosed,
    fixture.item, [&](quint64 generation) { if (generation == fixture.generation)
      state.closeAckNs = QtThreaded::monotonicNanos(); }, Qt::DirectConnection);
  const auto disconnectClose = qScopeGuard([&] { QObject::disconnect(closeConnection); });
  const auto connection = QObject::connect(fixture.item, &MpvAbstractItem::renderFailure, fixture.item,
    [&](quint64 generation, MpvAbstractItem::RenderFailureStage stage, int error) {
      if (generation != fixture.generation || stage != MpvAbstractItem::RenderFailureStage::Render ||
          error != MPV_ERROR_UNSUPPORTED || state.matchingFailures++) return;
      state.stopEntered = QtThreaded::monotonicNanos();
      const auto result = player.requestNativeStop();
      state.stopReturned = QtThreaded::monotonicNanos();
      state.stopStatus = int(result.status);
      state.stopError = result.error;
      state.forceStatus = mpv_set_property_string(fixture.core, "force-window", "no");
    }, Qt::DirectConnection);
  const auto disconnect = qScopeGuard([&] { QObject::disconnect(connection); });
  QVERIFY(fixture.item->armSyntheticFrameService(fixture.generation, deadline.deadlineNs.load()));
  DiagnosticFailureInjection::render = MPV_ERROR_UNSUPPORTED;
  QTRY_COMPARE_WITH_TIMEOUT(state.matchingFailures, 1, 10000);
  QCOMPARE(state.stopStatus, int(PlayerComponent::NativeStopResult::Status::Accepted));
  QCOMPARE(state.forceStatus, 0);
  QVERIFY(fixture.cleanup.confirmTerminal(fixture.core, canceled));
  observer.drain();
  QVERIFY(c2QuiescenceStop(observer.terminalEvents(), entry));
  QCOMPARE(canceled.count(), 1);
  QVERIFY(sameBinding());
  QJsonObject properties;
  QVERIFY(quiescenceProperties(fixture.core, properties));
  state.terminalNs = QtThreaded::monotonicNanos();
  QTRY_VERIFY_WITH_TIMEOUT(observedSkip(fixture, render), 10000);
  QVERIFY(fixture.item->requestSyntheticFrameServiceClose(fixture.generation, true));
  QTRY_VERIFY_WITH_TIMEOUT(!closed.isEmpty() || !closeFailed.isEmpty(), 10000);
  QCOMPARE(closeFailed.count(), 0);
  QCOMPARE(closed.count(), 1);
  QCOMPARE(closed.at(0).at(0).toULongLong(), fixture.generation);
  QVERIFY(observedSkip(fixture, render));
  QVERIFY(state.closeAckNs >= state.terminalNs &&
          DiagnosticFailureInjection::serviceCloseNs.load() >= state.terminalNs);
  QVERIFY(sameBinding());
  observer.drain();
  QVERIFY(c2QuiescenceStop(observer.terminalEvents(), entry));
  QCOMPARE(canceled.count(), 1);
  QCOMPARE(fixture.failure->count(), 1);
  QCOMPARE(DiagnosticFailureInjection::render.load(), 0);
  const auto record = quiescenceRecord(fixture, observer, entry, binding, render, state, properties,
                                       canceled.count(), closed.count());
  QVERIFY(finishQuiescence(fixture, observer, retired, sameBinding, state, record, evidence));
  done = true;
  deadline.join();
  QVERIFY(deadline.outcome.load() == C2aHoldGate::Outcome::Observed);
}
}

void TestC2RetirementNative::syntheticQuiescence()
{
  const QString root = qEnvironmentVariable("JELLYFIN_TEST_OWNED_ROOT");
  const QString evidence = qEnvironmentVariable("RECOVERY_EVIDENCE_DIR");
  const QString media = qEnvironmentVariable("RECOVERY_MEDIA");
  const QString rootPath = QFileInfo(root).canonicalFilePath();
  const QString evidencePath = QFileInfo(evidence).canonicalFilePath();
  const QString mediaPath = QFileInfo(media).canonicalFilePath();
  QVERIFY(!rootPath.isEmpty() && !evidencePath.isEmpty() && !mediaPath.isEmpty() && QFileInfo(media).isFile());
  for (const auto& path : {evidencePath, mediaPath}) {
    const QString relative = QDir(rootPath).relativeFilePath(path);
    QVERIFY(relative != ".." && !relative.startsWith("../") && !QDir::isAbsolutePath(relative));
  }
  QString error;
  QVERIFY2(MpvTestBootstrap::prepareMpvTestEnvironment(root, &error), qPrintable(error));
  QVERIFY2(prepareMpvTestProfile(evidence, &error), qPrintable(error));
  Q_INIT_RESOURCE(resources);
  QVERIFY(SettingsComponent::Get().componentInitialize());
  PlayerComponent player;
  C2aFixture fixture;
  QVERIFY2(fixture.initialize(player, &error), qPrintable(error));
  exerciseSyntheticQuiescence(player, fixture, media, evidence);
}

void TestC2RetirementNative::exerciseSyntheticQuiescence(PlayerComponent& player, C2aFixture& fixture,
                                                          const QString& media, const QString& evidence)
{
  C2EventObserver observer(fixture.core);
  QVERIFY(observer.client);
  QVERIFY(fixture.startPlayback(player, media));
  QVERIFY(fixture.proveAdvancing(player));
  QVERIFY(saveQuiescenceFrame(evidence + "/c2-fault-before1.png", fixture.playbackFirstFrame.image));
  QVERIFY(saveQuiescenceFrame(evidence + "/c2-fault-before2.png", fixture.playbackSecondFrame.image));
  observer.drain();
  const int64_t entry = observer.latestLoadedEntry();
  QVERIFY(entry > 0);
  const auto renders = fixture.item->observed->snapshot();
  QVERIFY(!renders.isEmpty());
  const auto render = renders.last();
  QVERIFY(render.thread && render.context && render.returned > render.entered);
  const auto binding = player.m_mpvGeneration;
  const bool ac3 = player.m_doAc3Transcoding;
  const auto sameBinding = [&] { return fixture.item->controller() == fixture.controller && player.m_mpv == fixture.controller &&
    fixture.controller->mpv() == fixture.core && player.m_mpvGeneration == binding &&
    player.m_doAc3Transcoding == ac3 && fixture.controller->eventMode() == MpvController::EventMode::External; };
  QVERIFY(sameBinding());
  runQuiescence(player, fixture, observer, entry, binding, render, sameBinding, evidence);
}
