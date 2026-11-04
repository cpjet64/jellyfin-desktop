#pragma once

#include <QString>
#include <QUrl>
#include <cmath>
#include <mpv/client.h>
#include "c2_event_observer.h"
#include "c2_capture_observation.h"

// Test-only recipe: one local clip and one possible recovery load. It never
// owns the player, controller, render generation, or mpv event consumer.
class C2ReplayRecipe
{
public:
  enum class Intent { Playing, Paused, Stopped };
  enum class Outcome { Continued, LoadAccepted, Cancelled, Failed };

  static C2CaptureObservation observeCapture(mpv_handle* core, const QString& localFile, int64_t playlistId)
  {
    C2CaptureObservation observation;
    observation.expectedPath = localFile;
    observation.entry = playlistId;
    char* path = nullptr;
    observation.pathStatus = mpv_get_property(core, "path", MPV_FORMAT_STRING, &path);
    observation.pathPresent = path != nullptr;
    if (path) {
      int size = 0;
      while (size < 4097 && path[size]) ++size;
      observation.pathTruncated = size > 4096;
      observation.rawPath = QString::fromUtf8(path, qMin(size, 4096));
      mpv_free(path);
    }
    observation.timeStatus = mpv_get_property(core, "time-pos", MPV_FORMAT_DOUBLE, &observation.position);
    observation.pauseStatus = mpv_get_property(core, "pause", MPV_FORMAT_FLAG, &observation.paused);
    return observation;
  }

  bool captureObservation(const C2CaptureObservation& observation, Intent latest)
  {
    if (!observation.accepted()) return false;
    file = observation.expectedPath.toUtf8();
    position = observation.position;
    wasPaused = observation.paused != 0;
    intent = latest;
    entryId = observation.entry;
    return true;
  }

  bool capture(mpv_handle* core, const QString& localFile, Intent latest, int64_t playlistId,
               C2CaptureObservation* recorded = nullptr)
  {
    const auto observation = observeCapture(core, localFile, playlistId);
    if (recorded) *recorded = observation;
    return captureObservation(observation, latest);
  }

  void stop() { intent = Intent::Stopped; }

  bool sameFile(mpv_handle* core) const
  {
    char* raw = mpv_get_property_string(core, "path");
    if (!raw) return false;
    const QString actual = QString::fromUtf8(raw);
    mpv_free(raw);
    return c2SamePath(actual, QString::fromUtf8(file), c2HostPathPlatform());
  }

  Outcome afterReady(mpv_handle* core, C2EventObserver& observer)
  {
    qInfo() << "C2 phase afterReady enter";
    if (intent == Intent::Stopped) return Outcome::Cancelled;
    observer.drain();
    qInfo() << "C2 phase before idle property";
    const auto idle = observer.flag("idle-active");
    qInfo() << "C2 phase after idle property" << idle.error << idle.value;
    if (idle.error < 0) return Outcome::Failed;
    if (!idle.value)
      return observer.latestStartedEntry() == entryId && observer.latestLoadedEntry() == entryId && sameFile(core)
          ? Outcome::Continued : Outcome::Failed;
    bool ended = false;
    for (const auto& event : observer.events)
      if (event.kind == MPV_EVENT_END_FILE && event.playlistId == entryId && event.insertCount == 0 &&
          ((event.reason == MPV_END_FILE_REASON_EOF && event.error == 0) ||
           (event.reason == MPV_END_FILE_REASON_ERROR && event.error < 0)))
        ended = true;
    if (!ended) return Outcome::Failed;
    if (attempted || file.isEmpty()) return Outcome::Failed;
    int pause = 1;
    qInfo() << "C2 phase before replay pause";
    if (mpv_set_property(core, "pause", MPV_FORMAT_FLAG, &pause) < 0) return Outcome::Failed;
    qInfo() << "C2 phase after replay pause";
    attempted = true;
    const char* command[] = {"loadfile", file.constData(), "replace", nullptr};
    qInfo() << "C2 phase before replay loadfile";
    const int result = mpv_command(core, command);
    qInfo() << "C2 phase after replay loadfile" << result;
    return result < 0 ? Outcome::Failed : Outcome::LoadAccepted;
  }

  bool restore(mpv_handle* core) const
  {
    if (intent == Intent::Stopped) return false;
    int pause = 1;
    qInfo() << "C2 phase before restore pause";
    if (mpv_set_property(core, "pause", MPV_FORMAT_FLAG, &pause) < 0) return false;
    qInfo() << "C2 phase before restore seek";
    const int result = mpv_set_property(core, "time-pos", MPV_FORMAT_DOUBLE, const_cast<double*>(&position));
    qInfo() << "C2 phase after restore seek" << result;
    return result >= 0;
  }

  bool resume(mpv_handle* core) const
  {
    if (intent != Intent::Playing) return false;
    int pause = 0;
    return mpv_set_property(core, "pause", MPV_FORMAT_FLAG, &pause) >= 0;
  }

  QByteArray file;
  double position = 0;
  int64_t entryId = 0;
  bool wasPaused = false;
  bool attempted = false;
  Intent intent = Intent::Stopped;
};
