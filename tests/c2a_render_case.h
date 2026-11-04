#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QFile>
#include <QUrl>
#include "c2_event_observer.h"
#include "c2a_fixture.h"

namespace {
bool prepareRenderInputs(const QString& root, const QString& evidence, const QString& media, QString* error)
{
  const QString rootPath = QFileInfo(root).canonicalFilePath();
  const QString evidencePath = QFileInfo(evidence).canonicalFilePath();
  const QString mediaPath = QFileInfo(media).canonicalFilePath();
  if (rootPath.isEmpty() || evidencePath.isEmpty() || mediaPath.isEmpty() || !QFileInfo(media).isFile()) return false;
  for (const auto& path : {evidencePath, mediaPath}) {
    const QString relative = QDir(rootPath).relativeFilePath(path);
    if (relative == ".." || relative.startsWith("../") || QDir::isAbsolutePath(relative)) return false;
  }
  return MpvTestBootstrap::prepareMpvTestEnvironment(root, error) && prepareMpvTestProfile(evidence, error);
}

bool savePreFaultFrames(const QList<QtThreaded::Frame>& frames, const QString& path)
{
  for (const auto& first : frames) for (const auto& second : frames) {
    if (first.sequence >= second.sequence || first.image.isNull() || second.image.isNull() ||
        first.image == second.image) continue;
    auto bright = [](const QImage& image) {
      int count = 0;
      for (int y = 0; y < image.height(); y += 4)
        for (int x = 0; x < image.width(); x += 4)
          if (qGray(image.pixel(x, y)) > 170) ++count;
      return count > 100;
    };
    if (bright(first.image) && bright(second.image))
      return first.image.save(path + "/c2a-render-before.png") &&
             second.image.save(path + "/c2a-render-before2.png");
  }
  return false;
}

QJsonObject observeMpvPhase(C2EventObserver& observer, mpv_handle* core, const QString& phase,
                            bool includeProperties = true)
{
  observer.drain();
  QJsonArray events;
  for (const auto& event : observer.events) {
    if (event.kind == MPV_EVENT_START_FILE || event.kind == MPV_EVENT_END_FILE)
      events.append(QJsonObject{{"kind", int(event.kind)}, {"entry_id", qint64(event.playlistId)},
        {"reason", event.reason}, {"error", event.error}, {"insert_id", qint64(event.insertId)},
        {"insert_count", event.insertCount}});
  }
  if (!includeProperties) return {{"phase", phase}, {"events", events}, {"properties_sampled", false}};
  char* vo = nullptr;
  const int voStatus = mpv_get_property(core, "current-vo", MPV_FORMAT_STRING, &vo);
  const QString voValue = vo ? QString::fromUtf8(vo) : QString();
  if (vo) mpv_free(vo);
  int idle = 0;
  const int idleStatus = mpv_get_property(core, "idle-active", MPV_FORMAT_FLAG, &idle);
  return {{"phase", phase}, {"events", events}, {"properties_sampled", true}, {"current_vo_status", voStatus},
          {"current_vo", voValue}, {"idle_status", idleStatus}, {"idle", idle != 0}};
}

bool validMpvPhase(const QJsonObject& phase)
{
  if (phase.value("properties_sampled").toBool()) {
    const int vo = phase.value("current_vo_status").toInt();
    if ((vo != 0 && vo != MPV_ERROR_PROPERTY_UNAVAILABLE) || phase.value("idle_status").toInt(-1) != 0) return false;
  }
  for (const auto& value : phase.value("events").toArray()) {
    const auto event = value.toObject();
    const int kind = event.value("kind").toInt(-1);
    const int error = event.value("error").toInt();
    if (kind == MPV_EVENT_START_FILE && error != 0) return false;
    if (kind != MPV_EVENT_START_FILE && kind != MPV_EVENT_END_FILE) return false;
    if (kind == MPV_EVENT_END_FILE) {
      const bool failed = event.value("reason").toInt() == MPV_END_FILE_REASON_ERROR;
      if ((failed && error >= 0) || (!failed && error != 0)) return false;
    }
  }
  return true;
}

QJsonArray errorMessages(const QSignalSpy& spy)
{
  QJsonArray messages;
  for (const auto& event : spy) messages.append(event.at(0).toString());
  return messages;
}

bool saveRenderEvidence(const QString& path, const QString& filename, const QJsonObject& evidence)
{
  QFile file(path + "/" + filename);
  if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
  const QByteArray bytes = QJsonDocument(evidence).toJson();
  return file.write(bytes) == bytes.size() && file.flush();
}
}

struct C2aRenderState
{
  C2aRenderState(PlayerComponent& player, C2aFixture& fixture)
    : controller(fixture.controller), core(fixture.core), generation(fixture.generation),
      observer(core), retired(fixture.item, &MpvAbstractItem::renderRetired), playerError(&player, &PlayerComponent::error),
      canceled(&player, &PlayerComponent::canceled), finished(&player, &PlayerComponent::finished)
  {
    ackConnection = QObject::connect(fixture.item, &MpvAbstractItem::renderRetired, fixture.item,
      [this](quint64 acknowledged) {
        if (acknowledged != generation) return;
        ackReceipt = QtThreaded::monotonicNanos();
        ackOnGui = QThread::currentThread() == QCoreApplication::instance()->thread();
        ++matchingAcks;
      }, Qt::DirectConnection);
  }
  ~C2aRenderState() { QObject::disconnect(ackConnection); }
  MpvController* controller;
  mpv_handle* core;
  quint64 generation;
  quint64 binding = 0;
  bool ac3 = false;
  qint64 positionBefore = 0;
  MpvRenderEvent render;
  C2EventObserver observer;
  QSignalSpy retired;
  QSignalSpy playerError;
  QSignalSpy canceled;
  QSignalSpy finished;
  QJsonArray phases;
  qint64 failureReceipt = 0;
  qint64 requestEntered = 0;
  qint64 requestReturned = 0;
  qint64 failureHandlerReturned = 0;
  qint64 failureUnwound = 0;
  qint64 ackReceipt = 0;
  int matchingAcks = 0;
  bool ackOnGui = false;
  QMetaObject::Connection ackConnection;
  bool failureOnGui = false;
  bool retirementRequested = false;
};

namespace {
QJsonObject renderTiming(const C2aRenderState& state)
{
  return {{"clock", "std::chrono::steady_clock nanoseconds since epoch"},
    {"sample_note", "atomic fields loaded independently at fixture return; zero means not observed"},
    {"generation", qint64(state.generation)},
    {"failure_queued_for_gui_ns", qint64(DiagnosticFailureInjection::failurePublishedNs.load())},
    {"failure_received_gui_ns", state.failureReceipt}, {"request_entered_ns", state.requestEntered},
    {"request_returned_ns", state.requestReturned}, {"handler_return_boundary_ns", state.failureHandlerReturned},
    {"failure_unwound_ns", state.failureUnwound},
    {"unregister_entered_ns", qint64(DiagnosticFailureInjection::unregisterEnteredNs.load())},
    {"unregister_returned_ns", qint64(DiagnosticFailureInjection::unregisterReturnedNs.load())},
    {"free_entered_ns", qint64(DiagnosticFailureInjection::freeEnteredNs.load())},
    {"free_returned_ns", qint64(DiagnosticFailureInjection::freeReturnedNs.load())},
    {"ack_emitted_ns", qint64(DiagnosticFailureInjection::ackEmittedNs.load())},
    {"matching_ack_received_ns", state.ackReceipt}, {"matching_ack_count", state.matchingAcks},
    {"matching_ack_on_gui", state.ackOnGui}};
}
}

void TestC2RetirementNative::renderFailure()
{
  const QString root = qEnvironmentVariable("JELLYFIN_TEST_OWNED_ROOT");
  const QString evidence = qEnvironmentVariable("RECOVERY_EVIDENCE_DIR");
  const QString media = qEnvironmentVariable("RECOVERY_MEDIA");
  QString error;
  QVERIFY2(prepareRenderInputs(root, evidence, media, &error), qPrintable(error));
  Q_INIT_RESOURCE(resources);
  QVERIFY(SettingsComponent::Get().componentInitialize());
  PlayerComponent player;
  C2aFixture fixture;
  QVERIFY2(fixture.initialize(player, &error), qPrintable(error));
  verifyRenderFailure(fixture, player, media, evidence);
}

void TestC2RetirementNative::verifyRenderFailure(C2aFixture& fixture, PlayerComponent& player,
                                                const QString& media, const QString& evidence)
{
  C2aRenderState state(player, fixture);
  const auto saveTiming = qScopeGuard([&] {
    if (!saveRenderEvidence(evidence, "c2a-render-timing.json", renderTiming(state)))
      QTest::qFail("could not save render timing evidence", __FILE__, __LINE__);
  });
  QCOMPARE(state.controller->eventMode(), MpvController::EventMode::External);
  state.binding = player.m_mpvGeneration;
  state.ac3 = player.m_doAc3Transcoding;
  QVERIFY(state.observer.client);
  QVERIFY(fixture.startPlayback(player, media));
  QVERIFY(fixture.proveAdvancing(player));
  QVERIFY(savePreFaultFrames(fixture.scene.frames(), evidence));
  const auto renderEvents = fixture.item->observed->snapshot();
  QVERIFY(!renderEvents.isEmpty());
  state.render = renderEvents.last();
  QVERIFY(state.render.thread != 0 && state.render.context != 0 && state.render.returned > state.render.entered);
  state.positionBefore = player.getPosition();
  QVERIFY(state.positionBefore > 500);
  injectRenderFailure(fixture, player, state, evidence);
  if (QTest::currentTestFailed()) return;
  retireRenderFailure(fixture, player, state, evidence);
}

void TestC2RetirementNative::injectRenderFailure(C2aFixture& fixture, PlayerComponent& player,
                                                C2aRenderState& state, const QString& evidence)
{
  state.phases.append(observeMpvPhase(state.observer, state.core, "before-injection"));
  QVERIFY(validMpvPhase(state.phases.last().toObject()));
  const auto connection = QObject::connect(fixture.item, &MpvAbstractItem::renderFailure, fixture.item,
    [&](quint64 generation, MpvAbstractItem::RenderFailureStage stage, int error) {
      if (state.retirementRequested || generation != state.generation ||
          stage != MpvAbstractItem::RenderFailureStage::Render || error != MPV_ERROR_UNSUPPORTED) return;
      state.failureReceipt = QtThreaded::monotonicNanos();
      state.failureOnGui = QThread::currentThread() == QCoreApplication::instance()->thread();
      if (!state.failureOnGui) return;
      state.retirementRequested = true;
      state.requestEntered = QtThreaded::monotonicNanos();
      fixture.item->requestRenderRetirement(generation);
      state.requestReturned = QtThreaded::monotonicNanos();
      state.failureHandlerReturned = QtThreaded::monotonicNanos();
    }, Qt::DirectConnection);
  const auto disconnect = qScopeGuard([&] { QObject::disconnect(connection); });
  DiagnosticFailureInjection::render = MPV_ERROR_UNSUPPORTED;
  QTRY_COMPARE_WITH_TIMEOUT(fixture.failure->count(), 1, 15000);
  state.failureUnwound = QtThreaded::monotonicNanos();
  QCOMPARE(fixture.failure->at(0).at(0).toULongLong(), state.generation);
  QCOMPARE(fixture.failure->at(0).at(1).value<MpvAbstractItem::RenderFailureStage>(),
           MpvAbstractItem::RenderFailureStage::Render);
  QCOMPARE(fixture.failure->at(0).at(2).toInt(), int(MPV_ERROR_UNSUPPORTED));
  QCOMPARE(DiagnosticFailureInjection::render.load(), 0);
  QCOMPARE(fixture.failure->count(), 1);
  QCOMPARE(fixture.item->controller(), state.controller);
  QCOMPARE(state.controller->eventMode(), MpvController::EventMode::External);
  QCOMPARE(state.controller->mpv(), state.core);
  QCOMPARE(player.m_mpv, state.controller);
  QCOMPARE(player.m_mpvGeneration, state.binding);
  QCOMPARE(player.m_doAc3Transcoding, state.ac3);
  QVERIFY(state.failureOnGui && state.retirementRequested);
  QVERIFY(state.failureReceipt > 0 && state.failureReceipt <= state.requestEntered &&
          state.requestEntered <= state.requestReturned && state.requestReturned <= state.failureHandlerReturned &&
          state.failureHandlerReturned <= state.failureUnwound);
  state.phases.append(observeMpvPhase(state.observer, state.core, "after-failure-handler", false));
  QVERIFY(validMpvPhase(state.phases.last().toObject()));
  const QJsonObject early{{"scenario", "render-failure"}, {"generation", qint64(state.generation)},
    {"injected_error", int(MPV_ERROR_UNSUPPORTED)}, {"player_errors", errorMessages(state.playerError)},
    {"reported_error", fixture.failure->at(0).at(2).toInt()},
    {"reported_stage", int(fixture.failure->at(0).at(1).value<MpvAbstractItem::RenderFailureStage>())},
    {"finished", state.finished.count()}, {"canceled", state.canceled.count()}, {"phases", state.phases},
    {"failure_receipt_ns", state.failureReceipt}, {"request_entered_ns", state.requestEntered},
    {"request_returned_ns", state.requestReturned}, {"handler_return_boundary_ns", state.failureHandlerReturned},
    {"failure_unwound_ns", state.failureUnwound},
    {"ack_count_at_snapshot", state.retired.count()}};
  QVERIFY(saveRenderEvidence(evidence, "c2a-render-observed.json", early));
}

void TestC2RetirementNative::retireRenderFailure(C2aFixture& fixture, PlayerComponent& player,
                                                C2aRenderState& state, const QString& evidence)
{
  QVERIFY(state.retirementRequested);
  QTRY_COMPARE_WITH_TIMEOUT(state.retired.count(), 1, 10000);
  const auto sameBinding = [&] { return player.m_mpv == state.controller && player.m_mpvGeneration == state.binding &&
      state.controller->mpv() == state.core && player.m_doAc3Transcoding == state.ac3; };
  QVERIFY(fixture.cleanup.confirmRetired(state.generation, state.retired, sameBinding));
  QCOMPARE(DiagnosticFailureInjection::frees.load(), 1);
  QVERIFY(DiagnosticFailureInjection::ackOrder.load() > 0);
  QCOMPARE(DiagnosticFailureInjection::freeThread.load(), state.render.thread);
  QCOMPARE(DiagnosticFailureInjection::freeCurrentContext.load(), state.render.context);
  QCOMPARE(DiagnosticFailureInjection::freeOriginalContext.load(), state.render.context);
  QCOMPARE(DiagnosticFailureInjection::freeGeneration.load(), state.generation);
  state.phases.append(observeMpvPhase(state.observer, state.core, "after-retirement-before-stop"));
  QVERIFY(validMpvPhase(state.phases.last().toObject()));
  QVERIFY(saveRenderEvidence(evidence, "c2a-render-retired.json", {{"phases", state.phases},
    {"player_errors", errorMessages(state.playerError)}, {"finished", state.finished.count()},
    {"canceled", state.canceled.count()}}));
  QVERIFY(!fatalDiagnostic.load());
  QVERIFY(fixture.cleanup.stopToTerminal(player, state.core, state.canceled));
  state.phases.append(observeMpvPhase(state.observer, state.core, "after-terminal-stop"));
  QVERIFY(validMpvPhase(state.phases.last().toObject()));
  state.observer.close();
  QVERIFY(!state.observer.client);
  QVERIFY(fixture.cleanup.finish());
  QVERIFY(player.m_mpv == nullptr && player.m_mpvGeneration > state.binding);
  QVERIFY(DiagnosticFailureInjection::ackOrder.load() < DiagnosticFailureInjection::itemOrder.load());
  QVERIFY(DiagnosticFailureInjection::itemOrder.load() < DiagnosticFailureInjection::coreOrder.load());
  QVERIFY(!fatalDiagnostic.load());
  const QJsonObject record{{"scenario", "render-failure"}, {"generation", qint64(state.generation)},
    {"injected_error", int(MPV_ERROR_UNSUPPORTED)}, {"failure_count", fixture.failure->count()},
    {"position_before_ms", state.positionBefore}, {"binding_generation", qint64(state.binding)}, {"ac3", state.ac3},
    {"controller_identity", QString::number(reinterpret_cast<quintptr>(state.controller), 16)},
    {"core_identity", QString::number(reinterpret_cast<quintptr>(state.core), 16)},
    {"free_thread", QString::number(DiagnosticFailureInjection::freeThread.load(), 16)},
    {"free_context", QString::number(DiagnosticFailureInjection::freeCurrentContext.load(), 16)},
    {"player_errors_after_stop", errorMessages(state.playerError)},
    {"finished_after_stop", state.finished.count()}, {"canceled_after_stop", state.canceled.count()},
    {"frees", DiagnosticFailureInjection::frees.load()},
    {"ack_order", DiagnosticFailureInjection::ackOrder.load()}, {"phases", state.phases}};
  QVERIFY(saveRenderEvidence(evidence, "c2a-render-failure.json", record));
}
