#pragma once

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QThread>
#include <QJsonObject>
#include <cstdlib>
#include <functional>
#include <mpv/client.h>
#include "c2/DiagnosticFailureInjection.h"
#include "player/PlayerComponent.h"
#include "mpv_thread_probe.h"

// Construct before test assertions. A live mpv render context is never
// unwound through Qt GL teardown when Stop or retirement cannot be verified.
class MpvThreadCleanup
{
public:
  MpvThreadCleanup(PlayerComponent& owner, QtThreaded::Scene& output) : player(owner), scene(output) {}
  ~MpvThreadCleanup() { if (!finish()) std::_Exit(EXIT_FAILURE); }
  void attach(ProbeItem* value, mpv_handle* handle, const std::function<bool()>& verify)
  {
    item = value;
    core = handle;
    sameBinding = verify;
    generation = item->renderGeneration();
    retired = std::make_unique<QSignalSpy>(item, &MpvAbstractItem::renderRetired);
  }
  void setChecks(const std::function<bool()>& before, const std::function<bool()>& after)
  { beforeStop = before; afterStop = after; }
  bool finish()
  {
    if (done) return true;
    if (item && !stopAndRetire()) return false;
    QString error;
    if (!scene.shutdown(&error)) { qCritical() << "scene shutdown failed" << error; return false; }
    done = true;
    shutdownAt = QtThreaded::monotonicNanos();
    return clean;
  }
  QJsonObject evidence() const
  {
    return {{"stop_entered", stopEntered}, {"stop_returned", stopReturned}, {"idle_confirmed", idleAt},
            {"retirement_requested", retirementAt}, {"retirement_acknowledged", ackAt},
            {"shutdown_returned", shutdownAt}, {"generation", qint64(generation)},
            {"stop_status", stopStatus}, {"stop_error", stopError}, {"idle_status", idleStatus},
            {"idle_value", idleValue}, {"ack_count", ackCount}};
  }
private:
  bool stopAndRetire()
  {
    if (!core || !retired || item->renderGeneration() != generation || !sameBinding()) return false;
    QCoreApplication::processEvents();
    if (beforeStop && !beforeStop()) clean = false;
    stopEntered = QtThreaded::monotonicNanos();
    const auto result = player.requestNativeStop();
    stopReturned = QtThreaded::monotonicNanos();
    stopStatus = int(result.status);
    stopError = result.error;
    qInfo() << "native Stop status/error" << int(result.status) << result.error;
    if (result.status != PlayerComponent::NativeStopResult::Status::Accepted) return false;
    QElapsedTimer wait;
    wait.start();
    int idle = 0, status = MPV_ERROR_GENERIC;
    while (wait.elapsed() < 10000) {
      status = mpv_get_property(core, "idle-active", MPV_FORMAT_FLAG, &idle);
      if (status < 0) break;
      if (idle) break;
      QCoreApplication::processEvents();
      QThread::msleep(10);
    }
    qInfo() << "native Stop idle status/value" << status << idle;
    idleStatus = status;
    idleValue = idle;
    if (status < 0 || !idle || item->renderGeneration() != generation || !sameBinding()) return false;
    QCoreApplication::processEvents();
    if (afterStop && !afterStop()) clean = false;
    idleAt = QtThreaded::monotonicNanos();
    retirementAt = QtThreaded::monotonicNanos();
    item->requestRenderRetirement(generation);
    wait.restart();
    while (retired->isEmpty() && wait.elapsed() < 10000) {
      QCoreApplication::processEvents();
      QThread::msleep(10);
    }
    const bool ok = retired->count() == 1 && retired->at(0).at(0).toULongLong() == generation &&
                    DiagnosticFailureInjection::frees.load() == 1 && item->renderGeneration() == generation && sameBinding();
    ackCount = retired->count();
    if (ok) ackAt = QtThreaded::monotonicNanos();
    qInfo() << "G1 retirement acknowledged and binding retained" << ok;
    return ok;
  }
  PlayerComponent& player;
  QtThreaded::Scene& scene;
  ProbeItem* item = nullptr;
  mpv_handle* core = nullptr;
  quint64 generation = 0;
  std::function<bool()> sameBinding;
  std::function<bool()> beforeStop, afterStop;
  std::unique_ptr<QSignalSpy> retired;
  bool done = false;
  bool clean = true;
  qint64 stopEntered = 0, stopReturned = 0, idleAt = 0, retirementAt = 0, ackAt = 0, shutdownAt = 0;
  int stopStatus = -1, stopError = 0, idleStatus = MPV_ERROR_GENERIC, idleValue = 0, ackCount = 0;
};
