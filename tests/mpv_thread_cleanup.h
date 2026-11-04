#pragma once

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QSignalSpy>
#include <QThread>
#include <cstdlib>
#include <functional>
#include <mpv/client.h>
#include "c2/DiagnosticFailureInjection.h"
#include "player/PlayerComponent.h"
#include "mpv_thread_probe.h"

// Failed or uncertain teardown stays contained in the test child process.
class MpvThreadCleanup
{
public:
  MpvThreadCleanup(PlayerComponent& owner, QtThreaded::Scene& output) : player(owner), scene(output) {}
  ~MpvThreadCleanup()
  {
    if (!attempted) finish();
    if (!resourcesSettled) std::_Exit(EXIT_FAILURE);
  }
  void attach(ProbeItem* value, mpv_handle* handle)
  {
    item = value;
    core = handle;
    generation = item->renderGeneration();
    retired = std::make_unique<QSignalSpy>(item, &MpvAbstractItem::renderRetired);
  }
  void renderMayExist() { uncertainRender = true; }
  void rendererReady() { ready = true; }
  void noteSubmission() { submissionAttempted = true; submissionAt = QtThreaded::monotonicNanos(); }
  void bind(const std::function<bool()>& verify, const std::function<int()>& canceledCount,
            const std::function<qint64()>& canceledTime)
  { sameBinding = verify; canceled = canceledCount; canceledAt = canceledTime; bound = true; }
  void setChecks(const std::function<bool()>& before, const std::function<bool()>& after)
  { beforeStop = before; afterStop = after; }
  bool finish()
  {
    if (attempted) return resourcesSettled && clean;
    attempted = true;
    if (item && uncertainRender && !ready) return false;
    if (item && ready && !retire()) return false;
    QString error;
    if (!scene.shutdown(&error)) { qCritical() << "scene shutdown failed" << error; return false; }
    resourcesSettled = true;
    shutdownAt = QtThreaded::monotonicNanos();
    return clean;
  }
  QJsonObject evidence() const
  {
    return {{"bound", bound}, {"renderer_ready", ready}, {"submission_attempted", submissionAttempted},
            {"submission_at", submissionAt}, {"stop_attempted", stopAttempted},
            {"resources_settled", resourcesSettled}, {"stop_entered", stopEntered},
            {"stop_returned", stopReturned}, {"terminal_confirmed", idleAt},
            {"canceled_received", canceledAt ? canceledAt() : cancelAt},
            {"canceled_count", canceled ? canceled() : cancelCount},
            {"retirement_requested", retirementAt}, {"retirement_acknowledged", ackAt},
            {"shutdown_returned", shutdownAt}, {"generation", qint64(generation)},
            {"stop_status", stopStatus}, {"stop_error", stopError}, {"idle_status", idleStatus},
            {"idle_value", idleValue}, {"ack_count", ackCount}, {"properties", properties}};
  }
private:
  QJsonObject readString(const char* name)
  {
    char* raw = nullptr;
    const int status = mpv_get_property(core, name, MPV_FORMAT_STRING, &raw);
    const QString value = raw ? QString::fromUtf8(raw) : QString();
    if (raw) mpv_free(raw);
    return {{"status", status}, {"value", value}, {"received", QtThreaded::monotonicNanos()}};
  }
  QJsonObject readFlag(const char* name)
  {
    int value = 0;
    const int status = mpv_get_property(core, name, MPV_FORMAT_FLAG, &value);
    return {{"status", status}, {"value", value}, {"received", QtThreaded::monotonicNanos()}};
  }
  bool prepareTerminal()
  {
    QCoreApplication::processEvents();
    if (beforeStop && !beforeStop()) clean = false;
    const auto force = readString("force-window");
    const auto vo = readString("current-vo");
    const auto configured = readFlag("vo-configured");
    properties.insert("before_force_window", force);
    properties.insert("before_current_vo", vo);
    properties.insert("before_vo_configured", configured);
    if (force["status"].toInt() < 0 || configured["status"].toInt() < 0 || canceled() != 0 ||
        (submissionAttempted && (vo["status"].toInt() < 0 || vo["value"].toString() != "libmpv")))
      clean = false;
    if (submissionAttempted && !stopPlayback()) return false;
    const int setStatus = mpv_set_property_string(core, "force-window", "no");
    properties.insert("force_window_set_no_status", setStatus);
    properties.insert("force_window_set_no_returned", QtThreaded::monotonicNanos());
    return setStatus >= 0 && awaitTerminal();
  }
  bool stopPlayback()
  {
    stopAttempted = true;
    stopEntered = QtThreaded::monotonicNanos();
    const auto result = player.requestNativeStop();
    stopReturned = QtThreaded::monotonicNanos();
    stopStatus = int(result.status);
    stopError = result.error;
    return result.status == PlayerComponent::NativeStopResult::Status::Accepted;
  }
  bool awaitTerminal()
  {
    QElapsedTimer wait;
    wait.start();
    while (wait.elapsed() < 10000) {
      QCoreApplication::processEvents();
      const auto force = readString("force-window");
      const auto vo = readString("current-vo");
      const auto idle = readFlag("idle-active");
      properties.insert("terminal_force_window", force);
      properties.insert("terminal_current_vo", vo);
      properties.insert("terminal_idle", idle);
      idleStatus = idle["status"].toInt();
      idleValue = idle["value"].toInt();
      cancelCount = canceled();
      cancelAt = canceledAt();
      if (force["status"].toInt() < 0 || idleStatus < 0 ||
          (vo["status"].toInt() < 0 && vo["status"].toInt() != MPV_ERROR_PROPERTY_UNAVAILABLE) ||
          cancelCount > (submissionAttempted ? 1 : 0)) return false;
      if (force["value"].toString() == "no" && idleValue && cancelCount == (submissionAttempted ? 1 : 0) &&
          vo["status"].toInt() == MPV_ERROR_PROPERTY_UNAVAILABLE) {
        idleAt = QtThreaded::monotonicNanos();
        if (afterStop && !afterStop()) clean = false;
        return true;
      }
      QThread::msleep(10);
    }
    return false;
  }
  bool retire()
  {
    if (!core || !retired || item->renderGeneration() != generation) return false;
    if (bound && (!sameBinding() || !prepareTerminal())) return false;
    if (!bound && readString("current-vo")["status"].toInt() != MPV_ERROR_PROPERTY_UNAVAILABLE) return false;
    retirementAt = QtThreaded::monotonicNanos();
    item->requestRenderRetirement(generation);
    QElapsedTimer wait;
    wait.start();
    while (retired->isEmpty() && wait.elapsed() < 10000) {
      QCoreApplication::processEvents();
      QThread::msleep(10);
    }
    const bool ok = retired->count() == 1 && retired->at(0).at(0).toULongLong() == generation &&
                    DiagnosticFailureInjection::frees.load() == 1 && item->renderGeneration() == generation &&
                    (!bound || sameBinding());
    ackCount = retired->count();
    if (ok) ackAt = QtThreaded::monotonicNanos();
    return ok;
  }
  PlayerComponent& player;
  QtThreaded::Scene& scene;
  ProbeItem* item = nullptr;
  mpv_handle* core = nullptr;
  quint64 generation = 0;
  std::function<bool()> sameBinding, beforeStop, afterStop;
  std::function<int()> canceled;
  std::function<qint64()> canceledAt;
  std::unique_ptr<QSignalSpy> retired;
  bool attempted = false, resourcesSettled = false, clean = true;
  bool uncertainRender = false, ready = false, bound = false, submissionAttempted = false, stopAttempted = false;
  qint64 submissionAt = 0;
  qint64 stopEntered = 0, stopReturned = 0, idleAt = 0, cancelAt = 0;
  qint64 retirementAt = 0, ackAt = 0, shutdownAt = 0;
  int stopStatus = -1, stopError = 0, idleStatus = MPV_ERROR_GENERIC, idleValue = 0, ackCount = 0, cancelCount = 0;
  QJsonObject properties;
};
