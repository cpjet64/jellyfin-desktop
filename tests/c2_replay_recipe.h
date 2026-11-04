#pragma once

#include <QString>
#include <QUrl>
#include <cmath>
#include <mpv/client.h>
#include "c2_event_observer.h"

// Test-only recipe: one local clip and one possible recovery load. It never
// owns the player, controller, render generation, or mpv event consumer.
class C2ReplayRecipe
{
public:
  enum class Intent { Playing, Paused, Stopped };
  enum class Outcome { Continued, LoadAccepted, Cancelled, Failed };

  bool capture(mpv_handle* core, const QString& localFile, Intent latest, int64_t playlistId)
  {
    const QByteArray encoded = localFile.toUtf8();
    char* path = mpv_get_property_string(core, "path");
    if (!path) return false;
    const QString actual = QString::fromUtf8(path);
    const QUrl url(actual);
    const bool same = (url.isLocalFile() ? url.toLocalFile() : actual) == localFile;
    mpv_free(path);
    int paused = 0;
    if (!same || mpv_get_property(core, "time-pos", MPV_FORMAT_DOUBLE, &position) < 0 ||
        mpv_get_property(core, "pause", MPV_FORMAT_FLAG, &paused) < 0 ||
        !std::isfinite(position) || position < 0) return false;
    file = encoded;
    wasPaused = paused != 0;
    intent = latest;
    entryId = playlistId;
    return true;
  }

  void stop() { intent = Intent::Stopped; }

  Outcome afterReady(mpv_handle* core, C2EventObserver& observer)
  {
    if (intent == Intent::Stopped) return Outcome::Cancelled;
    observer.drain();
    const auto idle = observer.flag("idle-active");
    if (idle.error < 0) return Outcome::Failed;
    if (!idle.value) return Outcome::Continued;
    bool ended = false;
    for (const auto& event : observer.events)
      if (event.kind == MPV_EVENT_END_FILE && event.playlistId == entryId && event.insertCount == 0 &&
          ((event.reason == MPV_END_FILE_REASON_EOF && event.error == 0) ||
           (event.reason == MPV_END_FILE_REASON_ERROR && event.error < 0)))
        ended = true;
    if (!ended) return Outcome::Failed;
    if (attempted || file.isEmpty()) return Outcome::Failed;
    int pause = 1;
    if (mpv_set_property(core, "pause", MPV_FORMAT_FLAG, &pause) < 0) return Outcome::Failed;
    attempted = true;
    const char* command[] = {"loadfile", file.constData(), "replace", nullptr};
    return mpv_command(core, command) < 0 ? Outcome::Failed : Outcome::LoadAccepted;
  }

  bool restore(mpv_handle* core) const
  {
    if (intent == Intent::Stopped) return false;
    int pause = 1;
    if (mpv_set_property(core, "pause", MPV_FORMAT_FLAG, &pause) < 0) return false;
    return mpv_set_property(core, "time-pos", MPV_FORMAT_DOUBLE,
                            const_cast<double*>(&position)) >= 0;
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
