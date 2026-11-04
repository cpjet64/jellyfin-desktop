#pragma once

#include "c2a_callback_hold.h"
#include "c2a_fixture.h"

namespace {
class DeferredUpdateRelease
{
public:
  ~DeferredUpdateRelease() { DiagnosticFailureInjection::holdDeferredUpdate = false; }
};

class ActiveEndObserver
{
public:
  explicit ActiveEndObserver(mpv_handle* core) : client(mpv_create_client(core, "c2a-active-observer")) {}
  ~ActiveEndObserver() { close(); }
  void drain()
  {
    while (client) {
      mpv_event* event = mpv_wait_event(client, 0);
      if (!event || event->event_id == MPV_EVENT_NONE) break;
      if (event->event_id == MPV_EVENT_END_FILE && event->data) {
        const auto* end = static_cast<mpv_event_end_file*>(event->data);
        ++endCount;
        reason = int(end->reason);
        error = end->error;
      }
    }
  }
  void close() { if (client) { mpv_destroy(client); client = nullptr; } }
  mpv_handle* client = nullptr;
  int endCount = 0, reason = -1, error = 0;
};

bool activeFrameVisible(const QImage& image)
{
  if (image.isNull()) return false;
  int bright = 0;
  for (int y = 0; y < image.height(); y += 4)
    for (int x = 0; x < image.width(); x += 4)
      if (qGray(image.pixel(x, y)) > 170) ++bright;
  return bright > 100;
}

bool saveActiveFrames(const C2aFixture& fixture, const QString& evidence)
{
  const auto& first = fixture.playbackFirstFrame;
  const auto& second = fixture.playbackSecondFrame;
  return first.sequence < second.sequence && activeFrameVisible(first.image) && activeFrameVisible(second.image) &&
    first.image != second.image && first.image.save(evidence + "/c2a-active-before1.png") &&
    second.image.save(evidence + "/c2a-active-before2.png");
}

bool saveActiveEvidence(const QString& evidence, const QString& name, const QJsonObject& record)
{
  QFile file(evidence + "/" + name);
  if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
  const QByteArray bytes = QJsonDocument(record).toJson();
  return file.write(bytes) == bytes.size() && file.flush();
}
}

void TestC2RetirementNative::activeRetirement()
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
  exerciseActive(fixture, player, evidence, media);
}

void TestC2RetirementNative::exerciseActive(C2aFixture& fixture, PlayerComponent& player,
                                            const QString& evidence, const QString& media)
{
  DeferredUpdateRelease deferred;
  ActiveEndObserver observer(fixture.core);
  C2aCallbackHold hold;
  QVERIFY(observer.client);
  const auto bindingGeneration = player.m_mpvGeneration;
  const bool ac3 = player.m_doAc3Transcoding;
  auto sameBinding = [&] {
    return player.m_mpv == fixture.controller && fixture.controller->mpv() == fixture.core &&
      fixture.controller->eventMode() == MpvController::EventMode::External &&
      player.m_mpvGeneration == bindingGeneration && player.m_doAc3Transcoding == ac3;
  };
  QVERIFY(sameBinding());
  QSignalSpy retired(fixture.item, &MpvAbstractItem::renderRetired);
  QSignalSpy canceled(&player, &PlayerComponent::canceled);
  QSignalSpy finished(&player, &PlayerComponent::finished);
  QSignalSpy playbackError(&player, &PlayerComponent::error);
  QVERIFY(fixture.startPlayback(player, media));
  QVERIFY(fixture.proveAdvancing(player));
  QVERIFY(saveActiveFrames(fixture, evidence));
  DiagnosticFailureInjection::holdDeferredUpdate = true;
  DiagnosticFailureInjection::deferNextQueuedUpdate = true;
  QTRY_COMPARE_WITH_TIMEOUT(DiagnosticFailureInjection::updatesDeferred.load(), 1, 5000);
  hold.configure(evidence, fixture.scene.audit().renderThread, fixture.generation);
  hold.arm();
  QTRY_VERIFY_WITH_TIMEOUT(hold.entered(), 5000);
  hold.observeAndRelease(fixture.generation);
  const qint64 requestedAt = QtThreaded::monotonicNanos();
  hold.markRequestEntered();
  fixture.item->requestRenderRetirement(fixture.generation);
  hold.markRequestReturned();
  QTRY_COMPARE_WITH_TIMEOUT(retired.count(), 1, 10000);
  hold.markAck();
  const qint64 acknowledgedAt = QtThreaded::monotonicNanos();
  hold.join();
  hold.snapshotGui();
  QVERIFY(hold.outcome.load() == C2aCallbackHold::Outcome::Observed && hold.heldAtUnregister.load());
  QCOMPARE(hold.observedGeneration.load(), fixture.generation);
  QVERIFY(hold.observedThread.load() && hold.observedUnregisterThread.load());
  QVERIFY(DiagnosticFailureInjection::callbackEnteredOrder.load() < DiagnosticFailureInjection::unregisterEnteredOrder.load());
  QVERIFY(DiagnosticFailureInjection::unregisterEnteredOrder.load() < DiagnosticFailureInjection::callbackHoldLeftOrder.load());
  QVERIFY(DiagnosticFailureInjection::callbackHoldLeftOrder.load() < DiagnosticFailureInjection::callbackExitedOrder.load());
  QVERIFY(DiagnosticFailureInjection::callbackExitedOrder.load() < DiagnosticFailureInjection::unregisteredOrder.load());
  QVERIFY(sameBinding());
  QCOMPARE(retired.at(0).at(0).toULongLong(), fixture.generation);
  QVERIFY(fixture.cleanup.confirmRetired(fixture.generation, retired, sameBinding));
  DiagnosticFailureInjection::holdDeferredUpdate = false;
  QTRY_COMPARE_WITH_TIMEOUT(DiagnosticFailureInjection::deferredUpdatesRejected.load(), 1, 5000);
  QCOMPARE(DiagnosticFailureInjection::ownersDestroyed.load(), 0);
  QVERIFY(sameBinding());
  finishActive(fixture, player, canceled, finished, playbackError, evidence, hold, observer, requestedAt, acknowledgedAt);
}

void TestC2RetirementNative::finishActive(C2aFixture& fixture, PlayerComponent& player,
                                          QSignalSpy& canceled, QSignalSpy& finished,
                                          QSignalSpy& playbackError, const QString& evidence,
                                          const C2aCallbackHold& hold, ActiveEndObserver& observer,
                                          qint64 requestedAt, qint64 acknowledgedAt)
{
  QTest::qWait(500);
  observer.drain();
  const int canceledBeforeCleanup = canceled.count();
  const int finishedBeforeCleanup = finished.count();
  const int errorsBeforeCleanup = playbackError.count();
  int idle = 0;
  QCOMPARE(mpv_get_property(fixture.core, "idle-active", MPV_FORMAT_FLAG, &idle), 0);
  const QJsonObject observed{{"generation", qint64(fixture.generation)},
    {"request_ns", requestedAt}, {"ack_ns", acknowledgedAt},
    {"core", QString::number(reinterpret_cast<quintptr>(fixture.core), 16)},
    {"controller", QString::number(reinterpret_cast<quintptr>(fixture.controller), 16)},
    {"binding_generation", qint64(player.m_mpvGeneration)},
    {"external_event_mode", fixture.controller->eventMode() == MpvController::EventMode::External},
    {"playback_start_ms", fixture.playbackStartPosition}, {"playback_end_ms", fixture.playbackEndPosition},
    {"first_frame_sequence", qint64(fixture.playbackFirstFrame.sequence)},
    {"second_frame_sequence", qint64(fixture.playbackSecondFrame.sequence)},
    {"first_frame_ns", fixture.playbackFirstFrame.monotonicNs},
    {"second_frame_ns", fixture.playbackSecondFrame.monotonicNs},
    {"held_at_unregister", hold.heldAtUnregister.load()},
    {"closed_order", DiagnosticFailureInjection::closedOrder.load()},
    {"callback_thread", QString::number(hold.observedThread.load(), 16)},
    {"unregister_thread", QString::number(hold.observedUnregisterThread.load(), 16)},
    {"callback_entered_order", DiagnosticFailureInjection::callbackEnteredOrder.load()},
    {"unregister_entered_order", DiagnosticFailureInjection::unregisterEnteredOrder.load()},
    {"callback_hold_left_order", DiagnosticFailureInjection::callbackHoldLeftOrder.load()},
    {"callback_accounted_order", DiagnosticFailureInjection::callbackExitedOrder.load()},
    {"unregister_returned_order", DiagnosticFailureInjection::unregisteredOrder.load()},
    {"free_order", DiagnosticFailureInjection::freedOrder.load()},
    {"pointer_cleared_order", DiagnosticFailureInjection::clearedOrder.load()},
    {"gui_ack_order", DiagnosticFailureInjection::ackOrder.load()},
    {"deferred_rejected", DiagnosticFailureInjection::deferredUpdatesRejected.load()},
    {"idle_before_cleanup", idle}, {"canceled_before_cleanup", canceledBeforeCleanup},
    {"finished_before_cleanup", finishedBeforeCleanup}, {"errors_before_cleanup", errorsBeforeCleanup},
    {"end_events_before_cleanup", observer.endCount}, {"end_reason", observer.reason}, {"end_error", observer.error}};
  QVERIFY(saveActiveEvidence(evidence, "c2a-active-observed.json", observed));
  if (canceledBeforeCleanup == 0) QVERIFY(fixture.cleanup.stopToTerminal(player, fixture.core, canceled));
  else {
    QCOMPARE(mpv_set_property_string(fixture.core, "force-window", "no"), 0);
    QVERIFY(fixture.cleanup.confirmTerminal(fixture.core, canceled));
  }
  observer.close();
  QVERIFY(fixture.cleanup.finish());
  QVERIFY(!fatalDiagnostic.load());
  const auto audit = fixture.scene.audit();
  const QJsonObject complete{{"worker_joined", audit.workerJoined}, {"context_destroyed", audit.contextDestroyed},
    {"surface_destroyed", audit.surfaceDestroyed}, {"cleanup_count", qint64(audit.cleanupCount)},
    {"cleanup_ack_order", qint64(audit.cleanupAckOrder)}, {"worker_join_order", qint64(audit.workerJoinedOrder)},
    {"surface_destroy_order", qint64(audit.surfaceDestroyedOrder)}};
  QVERIFY(saveActiveEvidence(evidence, "c2a-active-complete.json", complete));
}
