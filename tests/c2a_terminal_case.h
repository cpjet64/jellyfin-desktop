#pragma once

#include "c2a_fixture.h"

namespace {
bool terminalProperties(mpv_handle* core, QJsonObject& record)
{
  int idle = 0;
  char* vo = nullptr;
  char* force = nullptr;
  const int idleStatus = mpv_get_property(core, "idle-active", MPV_FORMAT_FLAG, &idle);
  const int voStatus = mpv_get_property(core, "current-vo", MPV_FORMAT_STRING, &vo);
  const int forceStatus = mpv_get_property(core, "force-window", MPV_FORMAT_STRING, &force);
  const QString forceValue = force ? QString::fromUtf8(force) : QString();
  const QString voValue = vo ? QString::fromUtf8(vo) : QString();
  if (vo) mpv_free(vo);
  if (force) mpv_free(force);
  record.insert("idle_status", idleStatus);
  record.insert("idle_value", idle);
  record.insert("vo_status", voStatus);
  record.insert("vo_value", voValue);
  record.insert("force_status", forceStatus);
  record.insert("force_value", forceValue);
  return idleStatus == 0 && idle == 1 && voStatus == MPV_ERROR_PROPERTY_UNAVAILABLE &&
    forceStatus == 0 && forceValue == "no";
}

bool saveTerminalEvidence(const QString& path, const QJsonObject& record)
{
  QFile file(path + "/c2a-terminal-retirement.json");
  if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
  const QByteArray bytes = QJsonDocument(record).toJson();
  return file.write(bytes) == bytes.size() && file.flush();
}

void recordTerminalAudit(QJsonObject& record, const QtThreaded::Audit& paused,
                         const QtThreaded::Audit& resumed, const QtThreaded::Audit& settled)
{
  record.insert("pause_thread", QString::number(paused.pauseAckThread, 16));
  record.insert("pause_context", QString::number(paused.pauseAckContext, 16));
  record.insert("resume_thread", QString::number(resumed.resumeAckThread, 16));
  record.insert("resume_context", QString::number(resumed.resumeAckContext, 16));
  record.insert("frames_started", qint64(paused.framesStarted));
  record.insert("frames_completed", qint64(paused.framesCompleted));
  record.insert("cleanup_ack_order", qint64(settled.cleanupAckOrder));
  record.insert("worker_join_order", qint64(settled.workerJoinedOrder));
  record.insert("surface_destroy_order", qint64(settled.surfaceDestroyedOrder));
}

bool observedRenderOnContext(ProbeItem* item, const QtThreaded::Audit& paused)
{
  for (const auto& event : item->observed->snapshot())
    if (event.returned > event.entered && event.thread == paused.pauseAckThread &&
        event.context == paused.pauseAckContext) return true;
  return false;
}
}

void TestC2RetirementNative::terminalRetirement()
{
  const QString root = qEnvironmentVariable("JELLYFIN_TEST_OWNED_ROOT");
  const QString evidence = qEnvironmentVariable("RECOVERY_EVIDENCE_DIR");
  const QString media = qEnvironmentVariable("RECOVERY_MEDIA");
  const QString rootPath = QFileInfo(root).canonicalFilePath();
  const QString evidencePath = QFileInfo(evidence).canonicalFilePath();
  QVERIFY(!rootPath.isEmpty() && !evidencePath.isEmpty() && QFileInfo(media).isFile());
  const QString relative = QDir(rootPath).relativeFilePath(evidencePath);
  QVERIFY(relative != ".." && !relative.startsWith("../") && !QDir::isAbsolutePath(relative));
  QString error;
  QVERIFY2(MpvTestBootstrap::prepareMpvTestEnvironment(root, &error), qPrintable(error));
  QVERIFY2(prepareMpvTestProfile(evidence, &error), qPrintable(error));
  Q_INIT_RESOURCE(resources);
  QVERIFY(SettingsComponent::Get().componentInitialize());
  PlayerComponent player;
  C2aFixture fixture;
  QVERIFY2(fixture.initialize(player, &error), qPrintable(error));
  runTerminalRetirement(player, fixture, evidence);
}

void TestC2RetirementNative::runTerminalRetirement(PlayerComponent& player, C2aFixture& fixture,
                                                    const QString& evidence)
{
  const QString media = qEnvironmentVariable("RECOVERY_MEDIA");
  QSignalSpy canceled(&player, &PlayerComponent::canceled);
  QVERIFY(fixture.startPlayback(player, media));
  const bool playbackAdvancing = fixture.proveAdvancing(player);
  QVERIFY(playbackAdvancing);
  const bool stopTerminal = fixture.cleanup.stopToTerminal(player, fixture.core, canceled);
  QVERIFY(stopTerminal);
  QJsonObject record{{"scenario", "terminal-retirement"}, {"generation", qint64(fixture.generation)},
    {"playback_advancing", playbackAdvancing}, {"stop_terminal", stopTerminal},
    {"canceled_count", canceled.count()}};
  QVERIFY(terminalProperties(fixture.core, record));
  const quint64 binding = player.m_mpvGeneration;
  const bool ac3 = player.m_doAc3Transcoding;
  const int hooksScheduled = player.m_c2HooksScheduled, hooksCompleted = player.m_c2HooksCompleted;
  record.insert("binding_generation", qint64(binding));
  record.insert("ac3_transcoding", ac3);
  record.insert("hooks_scheduled", hooksScheduled);
  record.insert("hooks_completed", hooksCompleted);
  const auto sameBinding = [&] { return fixture.item->controller() == fixture.controller &&
    fixture.controller->mpv() == fixture.core && player.m_mpv == fixture.controller &&
    player.m_mpvGeneration == binding && player.m_doAc3Transcoding == ac3 &&
    player.m_c2HooksScheduled == hooksScheduled && player.m_c2HooksCompleted == hooksCompleted; };
  QVERIFY(sameBinding());
  runTerminalExperiment(fixture, sameBinding, record, evidence);
}

void TestC2RetirementNative::runTerminalExperiment(C2aFixture& fixture,
    const std::function<bool()>& sameBinding, QJsonObject& record, const QString& evidence)
{
  QString error;
  QSignalSpy blocked(fixture.item, &MpvAbstractItem::renderRetirementBlocked);
  QSignalSpy retired(fixture.item, &MpvAbstractItem::renderRetired);
  QVERIFY2(fixture.scene.pauseFramePump(&error), qPrintable(error));
  const auto paused = fixture.scene.audit();
  QVERIFY(paused.framePumpPaused && paused.pauseAcks == 1 && paused.framesStarted == paused.framesCompleted);
  QVERIFY(paused.pauseAckThread == paused.renderThread && paused.pauseAckThread != paused.guiThread && paused.pauseAckContext);
  QVERIFY(observedRenderOnContext(fixture.item, paused));
  DiagnosticFailureInjection::blockNextRetire = true;
  record.insert("initial_request_ns", QtThreaded::monotonicNanos());
  fixture.item->requestRenderRetirement(fixture.generation);
  QTest::qWait(250); // Deliberate bounded observation while the worker is paused.
  QVERIFY(blocked.isEmpty() && retired.isEmpty() && sameBinding());
  const auto withheld = fixture.scene.audit();
  QVERIFY(withheld.framePumpPaused && withheld.framesStarted == paused.framesStarted &&
          withheld.framesCompleted == paused.framesCompleted);
  QCOMPARE(DiagnosticFailureInjection::frees.load(), 0);
  QCOMPARE(DiagnosticFailureInjection::ackOrder.load(), 0);
  record.insert("withheld_frees", DiagnosticFailureInjection::frees.load());
  record.insert("withheld_ack_order", DiagnosticFailureInjection::ackOrder.load());
  QVERIFY2(fixture.scene.resumeFramePump(&error), qPrintable(error));
  QTRY_COMPARE_WITH_TIMEOUT(blocked.count(), 1, 10000);
  record.insert("blocked_observed_ns", QtThreaded::monotonicNanos());
  QCOMPARE(blocked.at(0).at(0).toULongLong(), fixture.generation);
  QVERIFY(retired.isEmpty() && sameBinding());
  QCOMPARE(DiagnosticFailureInjection::frees.load(), 0);
  QCOMPARE(DiagnosticFailureInjection::ackOrder.load(), 0);
  record.insert("blocked_frees", DiagnosticFailureInjection::frees.load());
  record.insert("blocked_ack_order", DiagnosticFailureInjection::ackOrder.load());
  const auto resumed = fixture.scene.audit();
  QVERIFY(resumed.resumeAcks == 1 && resumed.resumeAckThread == paused.pauseAckThread &&
          resumed.resumeAckContext == paused.pauseAckContext);
  record.insert("retry_request_ns", QtThreaded::monotonicNanos());
  fixture.item->requestRenderRetirement(fixture.generation);
  QTRY_COMPARE_WITH_TIMEOUT(retired.count(), 1, 10000);
  record.insert("ack_observed_ns", QtThreaded::monotonicNanos());
  QCOMPARE(retired.at(0).at(0).toULongLong(), fixture.generation);
  QCOMPARE(DiagnosticFailureInjection::frees.load(), 1);
  QVERIFY(DiagnosticFailureInjection::ackOrder.load() > DiagnosticFailureInjection::freedOrder.load());
  QCOMPARE(DiagnosticFailureInjection::freeGeneration.load(), fixture.generation);
  QCOMPARE(DiagnosticFailureInjection::freeThread.load(), std::uintptr_t(paused.pauseAckThread));
  QCOMPARE(DiagnosticFailureInjection::freeCurrentContext.load(), std::uintptr_t(paused.pauseAckContext));
  QCOMPARE(DiagnosticFailureInjection::freeOriginalContext.load(), std::uintptr_t(paused.pauseAckContext));
  QVERIFY(fixture.cleanup.confirmRetired(fixture.generation, retired, sameBinding));
  record.insert("blocked_count", blocked.count());
  record.insert("retired_count", retired.count());
  record.insert("binding_preserved_at_ack", sameBinding());
  record.insert("free_thread", QString::number(DiagnosticFailureInjection::freeThread.load(), 16));
  record.insert("free_current_context", QString::number(DiagnosticFailureInjection::freeCurrentContext.load(), 16));
  record.insert("free_original_context", QString::number(DiagnosticFailureInjection::freeOriginalContext.load(), 16));
  record.insert("free_generation", qint64(DiagnosticFailureInjection::freeGeneration.load()));
  record.insert("closed_order", DiagnosticFailureInjection::closedOrder.load());
  record.insert("unregister_entered_order", DiagnosticFailureInjection::unregisterEnteredOrder.load());
  record.insert("unregistered_order", DiagnosticFailureInjection::unregisteredOrder.load());
  record.insert("freed_order", DiagnosticFailureInjection::freedOrder.load());
  record.insert("cleared_order", DiagnosticFailureInjection::clearedOrder.load());
  record.insert("ack_order", DiagnosticFailureInjection::ackOrder.load());
  QVERIFY(fixture.cleanup.finish());
  const auto settled = fixture.scene.audit();
  QVERIFY(settled.cleanupAckOrder > 0 && settled.cleanupAckOrder < settled.workerJoinedOrder &&
          settled.workerJoinedOrder < settled.surfaceDestroyedOrder);
  recordTerminalAudit(record, paused, resumed, settled);
  QVERIFY(!fatalDiagnostic.load());
  QVERIFY(saveTerminalEvidence(evidence, record));
}
