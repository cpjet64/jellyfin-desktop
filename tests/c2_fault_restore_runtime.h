#pragma once

#include "c2_terminal_recreation.h"
#include "c2_fault_restore_frame.h"
#include "c2_fault_restore_permission.h"
#include "c2a_hold_gate.h"
#include "c2_quiescence_observation.h"
#include "c2_quiescence_terminal_predicate.h"
#include <QScopeGuard>

// The inherited two-generation owner contains the process on uncertain teardown.
// Its ordinary recreation method intentionally remains unchanged.
class C2FaultRestoreRuntime : public C2TerminalRecreation
{
public:
  bool quiesce(PlayerComponent& player, QSignalSpy& canceled, quintptr thread, quintptr context,
               const std::function<bool()>& sameBinding, const QString& evidence)
  {
    C2QuiescenceObservation observation;
    std::atomic<bool> held{false}, done{false};
    C2aHoldGate gate(held, [&] { return done.load(); }, [](auto) {}, [](auto outcome) {
      if (outcome == C2aHoldGate::Outcome::Timeout) std::_Exit(EXIT_FAILURE);
    });
    const bool original = quiesceObserved(player, canceled, thread, context, sameBinding, evidence, observation, gate, done);
    return observation.finish(original, [&](const QJsonObject& record) {
      return save(evidence + "/c2-quiescence-observation.json", record);
    });
  }

  bool quiesceObserved(PlayerComponent& player, QSignalSpy& canceled, quintptr thread, quintptr context,
                        const std::function<bool()>& sameBinding, const QString& evidence,
                        C2QuiescenceObservation& observation, C2aHoldGate& gate, std::atomic<bool>& done)
  {
    gate.start(std::chrono::seconds(15));
    const auto checkedWait = [&](const QString& phase, const std::function<bool()>& predicate, int timeout = 10000) {
      return observation.wait(phase, [&](const std::function<bool()>& fn) { return wait(fn, timeout); }, predicate);
    };
    if (!checkedWait("guard-ready", [&] { return gate.ready.load(); }, 5000)) return false;
    observation.value("guard_deadline_telemetry_ns", qint64(gate.deadlineNs.load()));
    if (!observation.check("guard-arm", [&] { return gate.arm(); })) return false;
    int stopStatus = -1, forceStatus = -1, matching = 0;
    qint64 terminalNs = 0, closeAckNs = 0;
    QSignalSpy closed(item, &MpvAbstractItem::syntheticFrameServiceClosed);
    QSignalSpy closeFailed(item, &MpvAbstractItem::syntheticFrameServiceCloseFailed);
    const auto closeConnection = QObject::connect(item, &MpvAbstractItem::syntheticFrameServiceClosed, item,
      [&](quint64 generation) { observation.sample("close-signal", {{"generation", qint64(generation)}});
        if (generation == g1) closeAckNs = QtThreaded::monotonicNanos(); }, Qt::DirectConnection);
    const auto closeFailureConnection = QObject::connect(item, &MpvAbstractItem::syntheticFrameServiceCloseFailed, item,
      [&](quint64 generation) { observation.sample("close-failure-signal", {{"generation", qint64(generation)}}); }, Qt::DirectConnection);
    const auto failureConnection = QObject::connect(item, &MpvAbstractItem::renderFailure, item,
      [&](quint64 generation, MpvAbstractItem::RenderFailureStage stage, int error) {
        observation.sample("typed-fault", {{"generation", qint64(generation)}, {"stage", int(stage)}, {"error", error}});
        if (generation != g1 || stage != MpvAbstractItem::RenderFailureStage::Render ||
            error != MPV_ERROR_UNSUPPORTED || matching++) return;
        const auto stop = player.requestNativeStop();
        stopStatus = int(stop.status);
        observation.value("stop", QJsonObject{{"status", stopStatus}, {"error", stop.error}});
        forceStatus = mpv_set_property_string(core, "force-window", "no");
        observation.value("force_command_status", forceStatus);
      }, Qt::DirectConnection);
    const auto disconnect = qScopeGuard([&] { QObject::disconnect(closeConnection); QObject::disconnect(closeFailureConnection); QObject::disconnect(failureConnection); });
    if (!observation.check("service-arm", [&] { const auto value = gate.deadlineNs.load(); observation.value("guard_deadline_ns", qint64(value));
      return item->armSyntheticFrameService(g1, value); })) return false;
    DiagnosticFailureInjection::render = MPV_ERROR_UNSUPPORTED;
    if (!checkedWait("fault-wait", [&] { return matching != 0; }) ||
        !observation.check("matching-count", [&] { observation.value("matching", matching); return matching == 1; }) ||
        !observation.check("failure-count", [&] { const int count = failures->count(); observation.value("failures", count); return count == 1; }) ||
        !observation.check("stop-status", [&] { return stopStatus == int(PlayerComponent::NativeStopResult::Status::Accepted); }) ||
        !observation.check("force-status", [&] { return forceStatus == 0; }) ||
        !checkedWait("terminal-wait", [&] { return stoppedState(canceled, sameBinding, &observation); })) return false;
    terminalNs = QtThreaded::monotonicNanos();
    if (!checkedWait("service-wait", [&] { return serviceObserved(thread, context, &observation); }) ||
        !observation.check("service-close-request", [&] { return item->requestSyntheticFrameServiceClose(g1, true); }) ||
        !checkedWait("close-wait", [&] { const bool a = !closed.isEmpty(); observation.value("close_signal_present", a);
          if (a) return true; const bool b = !closeFailed.isEmpty(); observation.value("close_failure_present", b); return b; }) ||
        !observation.check("close-failed-empty", [&] { return closeFailed.isEmpty(); }) ||
        !observation.check("close-count", [&] { const int count = closed.count(); observation.value("close_count", count); return count == 1; }) ||
        !observation.check("close-generation", [&] { const auto value = closed.at(0).at(0).toULongLong(); observation.value("close_generation", qint64(value)); return value == g1; }) ||
        !observation.check("close-ack-time", [&] { observation.value("close_ack_ns", closeAckNs); observation.value("terminal_ns", terminalNs); return closeAckNs >= terminalNs; }) ||
        !observation.check("close-worker-after-terminal", [&] { const auto value = DiagnosticFailureInjection::serviceCloseNs.load(); observation.value("close_worker_first_ns", qint64(value)); return value >= terminalNs; }) ||
        !observation.check("close-worker-before-ack", [&] { const auto value = DiagnosticFailureInjection::serviceCloseNs.load(); observation.value("close_worker_second_ns", qint64(value)); return value <= closeAckNs; }) ||
        !observation.check("service-final", [&] { return serviceObserved(thread, context, &observation); }) ||
        !observation.check("terminal-final", [&] { return stoppedState(canceled, sameBinding, &observation); })) return false;
    return finishQuiescenceObserved(canceled, thread, context, sameBinding, evidence, observation,
                                     gate, done, terminalNs, closeAckNs, stopStatus, forceStatus);
  }

  bool finishQuiescenceObserved(QSignalSpy& canceled, quintptr thread, quintptr context,
      const std::function<bool()>& sameBinding, const QString& evidence, C2QuiescenceObservation& observation,
      C2aHoldGate& gate, std::atomic<bool>& done, qint64 terminalNs, qint64 closeAckNs, int stopStatus, int forceStatus)
  {
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
    if (!observation.check("service-evidence", [&] { return save(evidence + "/c2-restore-g1-service.json", service); }) ||
        !observation.check("retirement", [&] { QJsonObject trace; const bool result = retire(g1, 1, thread, context, sameBinding, &trace, &observation);
          observation.sample("retirement-detail", trace); return result; }) ||
        !observation.check("free-after-close", [&] { const auto value = DiagnosticFailureInjection::freeEnteredNs.load(); observation.value("free_entered_ns", qint64(value)); return value >= closeAckNs; })) return false;
    auto retiredEvidence = retirementEvidence();
    retiredEvidence.insert("free_entered_ns", qint64(DiagnosticFailureInjection::freeEnteredNs.load()));
    retiredEvidence.insert("free_returned_ns", qint64(DiagnosticFailureInjection::freeReturnedNs.load()));
    if (!observation.check("retirement-evidence", [&] { return save(evidence + "/c2-restore-g1-retired.json", retiredEvidence); })) return false;
    done = true;
    gate.join();
    return observation.check("guard-outcome", [&] { return gate.outcome.load() == C2aHoldGate::Outcome::Observed; });
  }

  bool stoppedState(QSignalSpy& canceled, const std::function<bool()>& sameBinding,
                    C2QuiescenceObservation* observation = nullptr)
  {
    int idle = 0;
    char* vo = nullptr;
    char* force = nullptr;
    const int idleStatus = mpv_get_property(core, "idle-active", MPV_FORMAT_FLAG, &idle);
    const int voStatus = mpv_get_property(core, "current-vo", MPV_FORMAT_STRING, &vo);
    const int forceStatus = mpv_get_property(core, "force-window", MPV_FORMAT_STRING, &force);
    const bool forceNo = force && QByteArray(force) == "no";
    const QString voValue = vo && observation ? QString::fromUtf8(vo).left(128) : QString();
    const QString forceValue = force ? QString::fromUtf8(force) : QString();
    if (vo) mpv_free(vo);
    if (force) mpv_free(force);
    QJsonObject sample{{"idle_status", idleStatus}, {"idle", idleStatus >= 0 ? QJsonValue(idle) : QJsonValue()},
      {"vo_status", voStatus}, {"vo_value", voStatus >= 0 && !voValue.isNull() ? QJsonValue(voValue) : QJsonValue()},
      {"force_status", forceStatus},
      {"force_value", forceStatus >= 0 && !forceValue.isNull() ? QJsonValue(forceValue.left(128)) : QJsonValue()},
      {"binding_evaluated", false}, {"events_evaluated", false}};
    const bool valid = c2QuiescenceTerminalPredicate(idleStatus, idle, voStatus, forceStatus, forceNo,
      [&] { const int count = canceled.count(); sample.insert("canceled", count); return count == 1; },
      [&] { const int count = failures->count(); sample.insert("failures", count); return count == 1; },
      [&] { const bool result = sameBinding(); sample.insert("binding_evaluated", true); sample.insert("binding", result); return result; },
      [&] { QJsonObject events; sample.insert("events_evaluated", true); const bool result = endedByStop(entry, observation ? &events : nullptr);
        if (observation) observation->sample("events", events); return result; });
    sample.insert("result", valid);
    if (observation) observation->sample("terminal-properties", sample);
    if (valid) {
      observedIdleStatus = idleStatus; observedIdle = idle; observedVoStatus = voStatus;
      observedForceStatus = forceStatus; observedForce = forceValue;
    }
    return valid;
  }

  bool serviceObserved(quintptr thread, quintptr context, C2QuiescenceObservation* observation = nullptr) const
  {
    using D = DiagnosticFailureInjection;
    QJsonObject sample;
    const auto read = [&](const char* key, auto& field) { const auto value = field.load(); sample.insert(key, qint64(value)); return value; };
    const bool result = read("pending", D::servicePendingCount) > 0 && read("skips", D::serviceSkipCount) > 0 &&
      read("ordinary_after_fault", D::ordinaryPostFaultCount) == 0 && read("result", D::serviceResult) == 0 &&
      read("generation", D::serviceGeneration) == g1 && read("thread", D::serviceThread) == thread &&
      read("context", D::serviceContext) == context && read("pending_ns_first", D::servicePendingNs) > 0 &&
      read("skip_entered_ns_first", D::serviceSkipEnteredNs) >= read("pending_ns_second", D::servicePendingNs) &&
      read("skip_returned_ns", D::serviceSkipReturnedNs) >= read("skip_entered_ns_second", D::serviceSkipEnteredNs);
    sample.insert("result_predicate", result);
    if (observation) observation->sample("service-atomics-sequential", sample);
    return result;
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
