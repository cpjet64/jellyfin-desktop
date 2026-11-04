#pragma once

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QThread>
#include <cstdlib>
#include <functional>
#include <mpv/client.h>
#include "c2/DiagnosticFailureInjection.h"
#include "mpv_thread_probe.h"
#include "player/PlayerComponent.h"
#include "c2_event_observer.h"

// A failed or ambiguous teardown remains contained in the fixture process.
class C2aFinalizer
{
public:
  enum class Phase { NoneAcquired, Live, Retired, Uncertain };
  explicit C2aFinalizer(QtThreaded::Scene& value) : scene(value) {}
  ~C2aFinalizer() { if (!attempted) finish(); if (!resourcesSettled) std::_Exit(EXIT_FAILURE); }
  void beginCreate(ProbeItem* value) { item = value; phase = Phase::Uncertain; }
  void confirmNoContext() { phase = Phase::NoneAcquired; }
  void confirmLive() { phase = Phase::Live; }
  bool confirmRetired(quint64 generation, const QSignalSpy& retired, const std::function<bool()>& sameBinding)
  {
    const int freed = DiagnosticFailureInjection::freedOrder.load();
    const int cleared = DiagnosticFailureInjection::clearedOrder.load();
    const int ack = DiagnosticFailureInjection::ackOrder.load();
    const auto renders = item ? item->observed->snapshot() : QVector<MpvRenderEvent>{};
    if (phase != Phase::Live || !item || retired.count() != 1 ||
        retired.at(0).at(0).toULongLong() != generation || item->renderGeneration() != generation ||
        DiagnosticFailureInjection::frees.load() != 1 || freed <= 0 || freed >= cleared || cleared >= ack ||
        renders.isEmpty() || !renders.last().context || renders.last().thread != scene.audit().renderThread ||
        DiagnosticFailureInjection::freeThread.load() != renders.last().thread ||
        DiagnosticFailureInjection::freeCurrentContext.load() != renders.last().context ||
        DiagnosticFailureInjection::freeOriginalContext.load() != renders.last().context ||
        DiagnosticFailureInjection::freeGeneration.load() != generation ||
        !sameBinding()) return false;
    phase = Phase::Retired;
    return true;
  }
  Phase currentPhase() const { return phase; }
  bool wasStopAttempted() const { return stopAttempted; }
  bool requestRetirement(quint64 generation, const std::function<bool()>& sameBinding)
  {
    if (retirementAttempted || phase != Phase::Live || !item || !sameBinding()) return false;
    retirementAttempted = true;
    QSignalSpy retired(item, &MpvAbstractItem::renderRetired);
    item->requestRenderRetirement(generation);
    QElapsedTimer timer;
    timer.start();
    while (retired.isEmpty() && timer.elapsed() < 10000) {
      QCoreApplication::processEvents();
      QThread::msleep(10);
    }
    return confirmRetired(generation, retired, sameBinding);
  }
  bool stopToTerminal(PlayerComponent& player, mpv_handle* core, QSignalSpy& canceled)
  {
    if (stopAttempted || !core || (phase != Phase::Live && phase != Phase::Retired) || canceled.count() != 0) return false;
    stopAttempted = true;
    stopEntered = QtThreaded::monotonicNanos();
    const auto stop = player.requestNativeStop();
    stopReturned = QtThreaded::monotonicNanos();
    stopStatus = int(stop.status);
    stopError = stop.error;
    if (stop.status != PlayerComponent::NativeStopResult::Status::Accepted ||
        mpv_set_property_string(core, "force-window", "no") < 0) return false;
    return confirmTerminal(core, canceled);
  }
  bool confirmTerminal(mpv_handle* core, QSignalSpy& canceled)
  {
    if (!core || (phase != Phase::Live && phase != Phase::Retired)) return false;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000) {
      QCoreApplication::processEvents();
      int idle = 0;
      char* vo = nullptr;
      char* force = nullptr;
      const int idleStatus = mpv_get_property(core, "idle-active", MPV_FORMAT_FLAG, &idle);
      const int voStatus = mpv_get_property(core, "current-vo", MPV_FORMAT_STRING, &vo);
      const int forceStatus = mpv_get_property(core, "force-window", MPV_FORMAT_STRING, &force);
      const bool terminal = idleStatus == 0 && idle == 1 && voStatus == MPV_ERROR_PROPERTY_UNAVAILABLE &&
        forceStatus == 0 && force && QByteArray(force) == "no" && canceled.count() == 1;
      if (vo) mpv_free(vo);
      if (force) mpv_free(force);
      if (terminal) { terminalObserved = true; return true; }
      QThread::msleep(10);
    }
    return false;
  }
  bool confirmErrorTerminal(mpv_handle* core, C2EventObserver& observer, int64_t entry,
                            const std::function<bool()>& sameBinding)
  {
    if (errorTerminalAttempted || terminalObserved || phase != Phase::Retired || !core ||
        !observer.client || !sameBinding()) return false;
    errorTerminalAttempted = true;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000) {
      QCoreApplication::processEvents();
      observer.drain();
      if (c2ErrorCleanupEligible(phase == Phase::Retired, sameBinding(), observer.terminalEvents(), entry)) break;
      QThread::msleep(10);
    }
    if (!c2ErrorCleanupEligible(phase == Phase::Retired, sameBinding(), observer.terminalEvents(), entry)) return false;
    if (mpv_set_property_string(core, "force-window", "no") < 0) return false;
    timer.restart();
    while (timer.elapsed() < 10000) {
      QCoreApplication::processEvents();
      observer.drain();
      if (!c2ErrorCleanupEligible(phase == Phase::Retired, sameBinding(), observer.terminalEvents(), entry)) return false;
      int idle = 0;
      char* vo = nullptr;
      char* force = nullptr;
      const int idleStatus = mpv_get_property(core, "idle-active", MPV_FORMAT_FLAG, &idle);
      const int voStatus = mpv_get_property(core, "current-vo", MPV_FORMAT_STRING, &vo);
      const int forceStatus = mpv_get_property(core, "force-window", MPV_FORMAT_STRING, &force);
      const bool terminal = c2ErrorTerminalProperties(idleStatus, idle, voStatus, forceStatus,
                                                       force && QByteArray(force) == "no");
      if (vo) mpv_free(vo);
      if (force) mpv_free(force);
      if (terminal) { terminalObserved = true; errorStopNotNeeded = true; return true; }
      QThread::msleep(10);
    }
    return false;
  }
  bool finish()
  {
    if (attempted) return resourcesSettled && valid;
    attempted = true;
    if (phase == Phase::Uncertain || phase == Phase::Live) return false;
    if (phase == Phase::Retired && !terminalObserved) return false;
    if (phase == Phase::NoneAcquired && (DiagnosticFailureInjection::frees.load() != 0 ||
                                         DiagnosticFailureInjection::ackOrder.load() != 0)) return false;
    QString error;
    if (!scene.shutdown(&error)) return false;
    const auto audit = scene.audit();
    if (!audit.workerJoined || !audit.contextDestroyed || !audit.surfaceDestroyed ||
        !audit.cleanupAckOrder || audit.cleanupAckOrder >= audit.workerJoinedOrder ||
        audit.workerJoinedOrder >= audit.surfaceDestroyedOrder) return false;
    resourcesSettled = true;
    item = nullptr; // The scene owns and destroys the item with its window.
    valid = true;
    return valid;
  }
private:
  QtThreaded::Scene& scene;
  ProbeItem* item = nullptr;
  Phase phase = Phase::NoneAcquired;
  bool attempted = false;
  bool resourcesSettled = false;
  bool valid = false;
  bool retirementAttempted = false;
  bool terminalObserved = false;
  bool errorTerminalAttempted = false;
  bool stopAttempted = false;
public:
  qint64 stopEntered = 0;
  qint64 stopReturned = 0;
  int stopStatus = -1;
  int stopError = 0;
  bool errorStopNotNeeded = false;
};
