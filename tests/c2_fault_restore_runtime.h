#pragma once

#include "c2_terminal_recreation.h"
#include "c2_fault_restore_frame.h"
#include "c2_fault_restore_permission.h"
#include "c2a_hold_gate.h"
#include <QScopeGuard>

// The inherited two-generation owner contains the process on uncertain teardown.
// Its ordinary recreation method intentionally remains unchanged.
class C2FaultRestoreRuntime : public C2TerminalRecreation
{
public:
  bool quiesce(PlayerComponent& player, QSignalSpy& canceled, quintptr thread, quintptr context,
               const std::function<bool()>& sameBinding, const QString& evidence)
  {
    std::atomic<bool> held{false}, done{false};
    C2aHoldGate gate(held, [&] { return done.load(); }, [](auto) {}, [](auto outcome) {
      if (outcome == C2aHoldGate::Outcome::Timeout) std::_Exit(EXIT_FAILURE);
    });
    gate.start(std::chrono::seconds(15));
    if (!wait([&] { return gate.ready.load(); }, 5000) || !gate.arm()) return false;
    int stopStatus = -1, forceStatus = -1, matching = 0;
    qint64 terminalNs = 0, closeAckNs = 0;
    QSignalSpy closed(item, &MpvAbstractItem::syntheticFrameServiceClosed);
    QSignalSpy closeFailed(item, &MpvAbstractItem::syntheticFrameServiceCloseFailed);
    const auto closeConnection = QObject::connect(item, &MpvAbstractItem::syntheticFrameServiceClosed, item,
      [&](quint64 generation) { if (generation == g1) closeAckNs = QtThreaded::monotonicNanos(); }, Qt::DirectConnection);
    const auto failureConnection = QObject::connect(item, &MpvAbstractItem::renderFailure, item,
      [&](quint64 generation, MpvAbstractItem::RenderFailureStage stage, int error) {
        if (generation != g1 || stage != MpvAbstractItem::RenderFailureStage::Render ||
            error != MPV_ERROR_UNSUPPORTED || matching++) return;
        stopStatus = int(player.requestNativeStop().status);
        forceStatus = mpv_set_property_string(core, "force-window", "no");
      }, Qt::DirectConnection);
    const auto disconnect = qScopeGuard([&] { QObject::disconnect(closeConnection); QObject::disconnect(failureConnection); });
    if (!item->armSyntheticFrameService(g1, gate.deadlineNs.load())) return false;
    DiagnosticFailureInjection::render = MPV_ERROR_UNSUPPORTED;
    if (!wait([&] { return matching != 0; }) || matching != 1 || failures->count() != 1 ||
        stopStatus != int(PlayerComponent::NativeStopResult::Status::Accepted) || forceStatus != 0 ||
        !wait([&] { return stoppedState(canceled, sameBinding); })) return false;
    terminalNs = QtThreaded::monotonicNanos();
    if (!wait([&] { return serviceObserved(thread, context); }) ||
        !item->requestSyntheticFrameServiceClose(g1, true) ||
        !wait([&] { return !closed.isEmpty() || !closeFailed.isEmpty(); }) || !closeFailed.isEmpty() ||
        closed.count() != 1 || closed.at(0).at(0).toULongLong() != g1 || closeAckNs < terminalNs ||
        DiagnosticFailureInjection::serviceCloseNs.load() < terminalNs ||
        DiagnosticFailureInjection::serviceCloseNs.load() > closeAckNs ||
        !serviceObserved(thread, context) || !stoppedState(canceled, sameBinding)) return false;
    const QJsonObject service{{"terminal_ns", terminalNs}, {"close_ack_ns", closeAckNs},
      {"entry", entry}, {"raw_end_reason", observedEndReason}, {"raw_end_error", observedEndError},
      {"raw_insert_id", observedInsertId}, {"raw_insert_count", observedInsertCount},
      {"stop_status", stopStatus}, {"force_status", forceStatus}, {"canceled", canceled.count()},
      {"idle_status", observedIdleStatus}, {"idle", observedIdle}, {"vo_status", observedVoStatus},
      {"force_property_status", observedForceStatus}, {"force_property", observedForce},
      {"pending", DiagnosticFailureInjection::servicePendingCount.load()},
      {"skips", DiagnosticFailureInjection::serviceSkipCount.load()},
      {"pending_ns", qint64(DiagnosticFailureInjection::servicePendingNs.load())},
      {"skip_entered_ns", qint64(DiagnosticFailureInjection::serviceSkipEnteredNs.load())},
      {"skip_returned_ns", qint64(DiagnosticFailureInjection::serviceSkipReturnedNs.load())},
      {"service_close_ns", qint64(DiagnosticFailureInjection::serviceCloseNs.load())},
      {"ordinary_after_fault", DiagnosticFailureInjection::ordinaryPostFaultCount.load()},
      {"generation", qint64(DiagnosticFailureInjection::serviceGeneration.load())},
      {"thread", QString::number(DiagnosticFailureInjection::serviceThread.load(), 16)},
      {"context", QString::number(DiagnosticFailureInjection::serviceContext.load(), 16)},
      {"result", DiagnosticFailureInjection::serviceResult.load()}};
    if (!save(evidence + "/c2-restore-g1-service.json", service) ||
        !retire(g1, 1, thread, context, sameBinding) ||
        DiagnosticFailureInjection::freeEnteredNs.load() < closeAckNs) return false;
    auto retiredEvidence = retirementEvidence();
    retiredEvidence.insert("free_entered_ns", qint64(DiagnosticFailureInjection::freeEnteredNs.load()));
    retiredEvidence.insert("free_returned_ns", qint64(DiagnosticFailureInjection::freeReturnedNs.load()));
    if (!save(evidence + "/c2-restore-g1-retired.json", retiredEvidence)) return false;
    done = true;
    gate.join();
    return gate.outcome.load() == C2aHoldGate::Outcome::Observed;
  }

  bool stoppedState(QSignalSpy& canceled, const std::function<bool()>& sameBinding)
  {
    int idle = 0;
    char* vo = nullptr;
    char* force = nullptr;
    const int idleStatus = mpv_get_property(core, "idle-active", MPV_FORMAT_FLAG, &idle);
    const int voStatus = mpv_get_property(core, "current-vo", MPV_FORMAT_STRING, &vo);
    const int forceStatus = mpv_get_property(core, "force-window", MPV_FORMAT_STRING, &force);
    const bool forceNo = force && QByteArray(force) == "no";
    const QString forceValue = force ? QString::fromUtf8(force) : QString();
    if (vo) mpv_free(vo);
    if (force) mpv_free(force);
    const bool valid = c2ErrorTerminalProperties(idleStatus, idle, voStatus, forceStatus, forceNo) &&
      canceled.count() == 1 && failures->count() == 1 && sameBinding() && endedByStop(entry);
    if (valid) {
      observedIdleStatus = idleStatus; observedIdle = idle; observedVoStatus = voStatus;
      observedForceStatus = forceStatus; observedForce = forceValue;
    }
    return valid;
  }

  bool serviceObserved(quintptr thread, quintptr context) const
  {
    using D = DiagnosticFailureInjection;
    return D::servicePendingCount.load() > 0 && D::serviceSkipCount.load() > 0 &&
      D::ordinaryPostFaultCount.load() == 0 && D::serviceResult.load() == 0 &&
      D::serviceGeneration.load() == g1 && D::serviceThread.load() == thread &&
      D::serviceContext.load() == context && D::servicePendingNs.load() > 0 &&
      D::serviceSkipEnteredNs.load() >= D::servicePendingNs.load() &&
      D::serviceSkipReturnedNs.load() >= D::serviceSkipEnteredNs.load();
  }
  bool recreateAfterSynthetic(quintptr thread, quintptr context)
  {
    if (failures->count() != 1 || DiagnosticFailureInjection::createCallCount.load() != firstCreateCount) return false;
    if (item->requestRenderCreation(g1) != MpvAbstractItem::CreationRequest::Accepted ||
        !wait([&] { return !created->isEmpty() || failures->count() != 1; })) return false;
    if (created->count() != 1 || failures->count() != 1 || created->at(0).at(0).toULongLong() != g1) return false;
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

  bool reloadPaused(const QByteArray& path, qint64 previous)
  {
    int pause = 1;
    if (mpv_set_property(core, "pause", MPV_FORMAT_FLAG, &pause) < 0) return false;
    observer->drain();
    eventOffset = observer->events.size();
    const char* command[] = {"loadfile", path.constData(), "replace", nullptr};
    if (mpv_command(core, command) < 0) return false;
    return wait([&] {
      observer->drain();
      entry = observer->latestLoadedEntry();
      return entry > 0 && entry != previous && oneLoadedEntry(entry) && paused();
    });
  }

  bool oneLoadedEntry(qint64 wanted)
  {
    observer->drain();
    int starts = 0, loaded = 0;
    for (std::size_t i = eventOffset; i < observer->events.size(); ++i) {
      const auto& event = observer->events[i];
      if (event.kind == MPV_EVENT_QUEUE_OVERFLOW || event.kind == MPV_EVENT_END_FILE) return false;
      if (event.kind == MPV_EVENT_START_FILE && (++starts != 1 || event.playlistId != wanted || event.error)) return false;
      if (event.kind == MPV_EVENT_FILE_LOADED && (++loaded != 1 || starts != 1 || event.error)) return false;
    }
    return starts == 1 && loaded == 1;
  }

  bool paused() const
  {
    int value = 0;
    return mpv_get_property(core, "pause", MPV_FORMAT_FLAG, &value) == 0 && value == 1;
  }

  bool exactSeek(double seconds)
  {
    const QByteArray value = QByteArray::number(seconds, 'f', 6);
    const char* command[] = {"seek", value.constData(), "absolute+exact", nullptr};
    return mpv_command(core, command) >= 0;
  }

  bool restoredFrame(double position, quint64 sequence, qint64 boundary, const QString& path)
  {
    return wait([&] {
      double actual = 0;
      if (!paused() || mpv_get_property(core, "time-pos", MPV_FORMAT_DOUBLE, &actual) < 0 ||
          !c2RestorePositionMatches(actual, position)) return false;
      for (const auto& frame : scene.frames()) {
        if (frame.sequence > sequence && frame.monotonicNs > boundary &&
            c2RestoreFrameMatches(frame.image, position)) {
          QFile file(path);
          if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || !frame.image.save(&file, "PNG") || !file.flush()) return false;
          restoredPosition = actual; restoredIndex = c2RestoreFrameIndex(frame.image);
          restoredSequence = frame.sequence; restoredNs = frame.monotonicNs; restoredBoundary = boundary;
          return true;
        }
      }
      return false;
    });
  }
  double restoredPosition = 0;
  int restoredIndex = -1;
  quint64 restoredSequence = 0;
  qint64 restoredNs = 0, restoredBoundary = 0;
};
