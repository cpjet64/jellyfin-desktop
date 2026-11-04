#pragma once

#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QThread>
#include <QUrl>
#include <cstdlib>
#include <functional>
#include <memory>
#include "c2_event_observer.h"
#include "c2/DiagnosticFailureInjection.h"
#include "mpv_thread_probe.h"

// This separate control owns two generations; it never resets C2a counters.
class C2TerminalRecreation
{
public:
  ~C2TerminalRecreation() { if (armed && !finished) std::_Exit(EXIT_FAILURE); }
  bool initialize(PlayerComponent& player, QString* error)
  {
    if (!player.componentInitialize()) return false;
    armed = true;
    if (!scene.initialize(error)) return false;
    item = new ProbeItem(scene.contentItem());
    item->setObjectName("video");
    item->setSize(QSizeF(640, 360));
    controller = item->controller();
    core = controller ? controller->mpv() : nullptr;
    if (!core) return false;
    g1 = item->renderGeneration();
    ready = std::make_unique<QSignalSpy>(item, &MpvAbstractItem::ready);
    retired = std::make_unique<QSignalSpy>(item, &MpvAbstractItem::renderRetired);
    created = std::make_unique<QSignalSpy>(item, &MpvAbstractItem::renderCreated);
    QObject::connect(item, &MpvAbstractItem::renderCreated, item,
      [this](quint64 retiredGeneration, quint64 createdGeneration) {
        if (retiredGeneration != g1 || creationAckNs != 0) return;
        creationAckNs = QtThreaded::monotonicNanos();
        ackOldGeneration = retiredGeneration;
        ackNewGeneration = createdGeneration;
        creationAckOnGui = QThread::currentThread() == QCoreApplication::instance()->thread();
      }, Qt::DirectConnection);
    failures = std::make_unique<QSignalSpy>(item, &MpvAbstractItem::renderFailure);
    observer = std::make_unique<C2EventObserver>(core);
    if (!observer->client || !scene.sync(error) || !wait([&] { return !ready->isEmpty(); })) return false;
    if (ready->count() != 1 || !failures->isEmpty()) return false;
    player.setWindow(scene.contentItem()->window());
    return true;
  }

  bool wait(const std::function<bool()>& predicate, int timeout = 10000)
  {
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < timeout) {
      QCoreApplication::processEvents();
      if (predicate()) return true;
      QThread::msleep(10);
    }
    return false;
  }

  bool play(PlayerComponent& player, const QString& media, qint64 previousEntry)
  {
    observer->drain();
    eventOffset = observer->events.size();
    if (!player.load(QUrl::fromLocalFile(media).toString(), {{"autoplay", true}}, {{"type", "video"}})) return false;
    return wait([&] {
      observer->drain();
      entry = observer->latestLoadedEntry();
      return entry > 0 && entry != previousEntry;
    });
  }

  static bool visible(const QImage& image)
  {
    if (image.isNull()) return false;
    int bright = 0;
    for (int y = 0; y < image.height(); y += 4)
      for (int x = 0; x < image.width(); x += 4)
        if (qGray(image.pixel(x, y)) > 170) ++bright;
    return bright > 100;
  }

  bool moving(PlayerComponent& player, const QString& path, quint64 afterSequence)
  {
    qint64 start = -1;
    qint64 startNs = 0;
    return wait([&] {
      const qint64 position = player.getPosition();
      const qint64 positionNs = QtThreaded::monotonicNanos();
      if (position > 500 && start < 0) { start = position; startNs = positionNs; }
      const auto frames = scene.frames();
      for (const auto& first : frames) for (const auto& second : frames) {
        if (start >= 0 && position > start + 200 && first.sequence > afterSequence &&
            first.sequence < second.sequence && visible(first.image) && visible(second.image) &&
            first.image != second.image && first.monotonicNs >= startNs && second.monotonicNs <= positionNs &&
            first.monotonicNs > playBoundary) {
          firstFrame = first; secondFrame = second; startPosition = start; endPosition = position;
          return first.image.save(path + "-1.png") && second.image.save(path + "-2.png");
        }
      }
      return false;
    });
  }

  bool endedByStop(qint64 wanted)
  {
    observer->drain();
    bool started = false, loaded = false, ended = false;
    for (std::size_t index = eventOffset; index < observer->events.size(); ++index) {
      const auto& event = observer->events[index];
      if (event.kind == MPV_EVENT_QUEUE_OVERFLOW) return false;
      if (event.kind == MPV_EVENT_START_FILE) {
        if (started || event.playlistId != wanted || event.error != 0) return false;
        started = true;
      } else if (event.kind == MPV_EVENT_FILE_LOADED) {
        if (!started || loaded || ended || event.error != 0) return false;
        loaded = true;
      } else if (event.kind == MPV_EVENT_END_FILE) {
        if (!started || !loaded || ended || event.playlistId != wanted || event.reason != MPV_END_FILE_REASON_STOP ||
            event.error != 0 || event.insertId != 0 || event.insertCount != 0) return false;
        ended = true;
        observedEndReason = event.reason; observedEndError = event.error;
        observedInsertId = event.insertId; observedInsertCount = event.insertCount;
      }
    }
    return started && loaded && ended;
  }

  bool terminal(PlayerComponent& player, QSignalSpy& canceled, int previousCanceled)
  {
    const auto result = player.requestNativeStop();
    if (result.status != PlayerComponent::NativeStopResult::Status::Accepted ||
        mpv_set_property_string(core, "force-window", "no") < 0) return false;
    const auto framesBefore = scene.audit().framesCompleted;
    const auto snapshot = [&] {
      int idle = 0;
      char* vo = nullptr;
      char* force = nullptr;
      const int idleStatus = mpv_get_property(core, "idle-active", MPV_FORMAT_FLAG, &idle);
      const int voStatus = mpv_get_property(core, "current-vo", MPV_FORMAT_STRING, &vo);
      const int forceStatus = mpv_get_property(core, "force-window", MPV_FORMAT_STRING, &force);
      const bool ok = idleStatus == 0 && idle && voStatus == MPV_ERROR_PROPERTY_UNAVAILABLE &&
        forceStatus == 0 && force && QByteArray(force) == "no" && canceled.count() == previousCanceled + 1 &&
        scene.audit().framesCompleted > framesBefore && endedByStop(entry);
      if (ok) {
        observedIdleStatus = idleStatus; observedIdle = idle; observedVoStatus = voStatus;
        observedForceStatus = forceStatus; observedForce = QString::fromUtf8(force);
        terminalObservedNs = QtThreaded::monotonicNanos();
      }
      if (vo) mpv_free(vo);
      if (force) mpv_free(force);
      return ok;
    };
    if (!wait(snapshot)) return false;
    QElapsedTimer stable;
    stable.start();
    while (stable.elapsed() < 300) {
      QCoreApplication::processEvents();
      if (!snapshot()) return false;
      QThread::msleep(10);
    }
    return true;
  }

  bool retire(quint64 generation, int expectedCount, quintptr thread, quintptr context,
              const std::function<bool()>& sameBinding)
  {
    if (!sameBinding() || retired->count() != expectedCount - 1) return false;
    retireRequestedNs = QtThreaded::monotonicNanos();
    item->requestRenderRetirement(generation);
    if (!wait([&] { return retired->count() >= expectedCount; })) return false;
    retireAcknowledgedNs = QtThreaded::monotonicNanos();
    return retired->count() == expectedCount && retired->at(expectedCount - 1).at(0).toULongLong() == generation &&
      DiagnosticFailureInjection::frees.load() == expectedCount &&
      DiagnosticFailureInjection::freedOrder.load() > 0 &&
      DiagnosticFailureInjection::freedOrder.load() < DiagnosticFailureInjection::clearedOrder.load() &&
      DiagnosticFailureInjection::clearedOrder.load() < DiagnosticFailureInjection::ackOrder.load() &&
      DiagnosticFailureInjection::freeGeneration.load() == generation &&
      DiagnosticFailureInjection::freeThread.load() == thread &&
      DiagnosticFailureInjection::freeCurrentContext.load() == context &&
      DiagnosticFailureInjection::freeOriginalContext.load() == context && sameBinding();
  }

  bool captureFirstCreation(quintptr thread, quintptr context)
  {
    firstCreateCount = DiagnosticFailureInjection::createCallCount.load();
    firstCreateGeneration = DiagnosticFailureInjection::createCallGeneration.load();
    firstCreateThread = DiagnosticFailureInjection::createCallThread.load();
    firstCreateContext = DiagnosticFailureInjection::createCallContext.load();
    firstCreateEnteredNs = DiagnosticFailureInjection::createCallEnteredNs.load();
    firstCreateReturnedNs = DiagnosticFailureInjection::createCallReturnedNs.load();
    firstCreateResult = DiagnosticFailureInjection::createCallResult.load();
    return firstCreateCount == 1 && firstCreateGeneration == g1 && firstCreateThread == thread &&
      firstCreateContext == context && firstCreateResult == 0 && firstCreateEnteredNs > 0 &&
      firstCreateReturnedNs >= firstCreateEnteredNs;
  }

  bool recreate(quintptr thread, quintptr context)
  {
    if (DiagnosticFailureInjection::createCallCount.load() != firstCreateCount) return false;
    if (item->requestRenderCreation(g1) != MpvAbstractItem::CreationRequest::Accepted ||
        !wait([&] { return !created->isEmpty() || !failures->isEmpty(); })) return false;
    if (created->count() != 1 || !failures->isEmpty() || created->at(0).at(0).toULongLong() != g1) return false;
    g2 = created->at(0).at(1).toULongLong();
    const auto entered = DiagnosticFailureInjection::createCallEnteredNs.load();
    const auto returned = DiagnosticFailureInjection::createCallReturnedNs.load();
    return g2 && g2 != g1 && item->renderGeneration() == g2 && creationAckOnGui &&
      ackOldGeneration == g1 && ackNewGeneration == g2 &&
      DiagnosticFailureInjection::createCallCount.load() == firstCreateCount + 1 &&
      DiagnosticFailureInjection::createCallGeneration.load() == g2 &&
      DiagnosticFailureInjection::createCallThread.load() == thread &&
      DiagnosticFailureInjection::createCallContext.load() == context &&
      DiagnosticFailureInjection::createCallResult.load() == 0 && entered > 0 &&
      returned >= entered && creationAckNs >= returned;
  }

  bool sameRenderContextAfter(qint64 boundary, quintptr thread, quintptr context)
  {
    return wait([&] {
      for (const auto& event : item->observed->snapshot())
        if (event.entered > boundary && event.returned > event.entered &&
            event.thread == thread && event.context == context) return true;
      return false;
    });
  }

  bool save(const QString& path, const QJsonObject& value)
  {
    QFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
    const auto bytes = QJsonDocument(value).toJson();
    return output.write(bytes) == bytes.size() && output.flush();
  }

  QJsonObject terminalEvidence(quint64 generation) const
  {
    return {{"entry", entry}, {"generation", qint64(generation)}, {"start_position_ms", startPosition},
      {"end_position_ms", endPosition}, {"raw_end_reason", observedEndReason}, {"raw_end_error", observedEndError},
      {"raw_insert_id", observedInsertId}, {"raw_insert_count", observedInsertCount},
      {"idle_status", observedIdleStatus}, {"idle", observedIdle != 0}, {"current_vo_status", observedVoStatus},
      {"force_window_status", observedForceStatus}, {"force_window", observedForce},
      {"terminal_observed_ns", terminalObservedNs}};
  }

  QJsonObject retirementEvidence() const
  {
    return {{"request_ns", retireRequestedNs}, {"ack_observed_ns", retireAcknowledgedNs},
      {"generation", qint64(DiagnosticFailureInjection::freeGeneration.load())},
      {"free_count", DiagnosticFailureInjection::frees.load()}, {"ack_count", retired->count()},
      {"free_thread", QString::number(DiagnosticFailureInjection::freeThread.load(), 16)},
      {"free_context", QString::number(DiagnosticFailureInjection::freeCurrentContext.load(), 16)},
      {"original_context", QString::number(DiagnosticFailureInjection::freeOriginalContext.load(), 16)},
      {"free_order", DiagnosticFailureInjection::freedOrder.load()},
      {"clear_order", DiagnosticFailureInjection::clearedOrder.load()},
      {"ack_order", DiagnosticFailureInjection::ackOrder.load()}};
  }

  QJsonObject creationEvidence() const
  {
    return {{"g1_count_baseline", firstCreateCount}, {"g2_count", DiagnosticFailureInjection::createCallCount.load()},
      {"g1_generation", qint64(g1)}, {"g2_generation", qint64(g2)},
      {"g1_actual_generation", qint64(firstCreateGeneration)}, {"g1_actual_result", firstCreateResult},
      {"g1_actual_thread", QString::number(firstCreateThread, 16)},
      {"g1_actual_context", QString::number(firstCreateContext, 16)},
      {"g1_actual_entered_ns", firstCreateEnteredNs}, {"g1_actual_returned_ns", firstCreateReturnedNs},
      {"actual_generation", qint64(DiagnosticFailureInjection::createCallGeneration.load())},
      {"actual_thread", QString::number(DiagnosticFailureInjection::createCallThread.load(), 16)},
      {"actual_context", QString::number(DiagnosticFailureInjection::createCallContext.load(), 16)},
      {"actual_result", DiagnosticFailureInjection::createCallResult.load()},
      {"actual_entered_ns", qint64(DiagnosticFailureInjection::createCallEnteredNs.load())},
      {"actual_returned_ns", qint64(DiagnosticFailureInjection::createCallReturnedNs.load())},
      {"matching_gui_ack_ns", creationAckNs}, {"ack_on_gui", creationAckOnGui}};
  }

  bool finish()
  {
    observer->close();
    QString error;
    if (!scene.shutdown(&error)) return false;
    const auto audit = scene.audit();
    if (!audit.workerJoined || !audit.contextDestroyed || !audit.surfaceDestroyed ||
        !audit.cleanupAckOrder || audit.cleanupAckOrder >= audit.workerJoinedOrder ||
        audit.workerJoinedOrder >= audit.surfaceDestroyedOrder) return false;
    finished = true;
    return true;
  }

  QtThreaded::Scene scene;
  ProbeItem* item = nullptr;
  MpvController* controller = nullptr;
  mpv_handle* core = nullptr;
  quint64 g1 = 0, g2 = 0;
  qint64 entry = 0, playBoundary = 0, startPosition = 0, endPosition = 0;
  qint64 observedInsertId = 0, terminalObservedNs = 0, retireRequestedNs = 0, retireAcknowledgedNs = 0;
  qint64 creationAckNs = 0;
  qint64 firstCreateEnteredNs = 0, firstCreateReturnedNs = 0;
  quint64 ackOldGeneration = 0, ackNewGeneration = 0;
  quint64 firstCreateGeneration = 0;
  quintptr firstCreateThread = 0, firstCreateContext = 0;
  int firstCreateCount = 0, firstCreateResult = -1;
  bool creationAckOnGui = false;
  int observedEndReason = -1, observedEndError = 0, observedInsertCount = 0;
  int observedIdleStatus = -1, observedIdle = 0, observedVoStatus = 0, observedForceStatus = -1;
  QString observedForce;
  std::size_t eventOffset = 0;
  QtThreaded::Frame firstFrame, secondFrame;
  std::unique_ptr<QSignalSpy> ready, retired, created, failures;
  std::unique_ptr<C2EventObserver> observer;
private:
  bool armed = false, finished = false;
};
