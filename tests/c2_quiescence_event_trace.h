#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <functional>
#include "c2_event_observer.h"

inline void c2TraceEvent(QJsonObject& trace, QJsonArray& events, const QJsonObject& event)
{
  if (events.size() < 32) events.append(event);
  else trace.insert("events_truncated", true);
  trace.insert("events", events);
}

inline void c2TraceRejection(QJsonObject& trace, qint64 index, const char* reason, const QJsonObject& event)
{
  trace.insert("rejected_index", index);
  trace.insert("rejection", reason);
  trace.insert("rejected_event", event);
}

inline bool c2EvaluateStopEvents(const std::vector<C2EventObserver::Event>& source, std::size_t offset, qint64 wanted,
    QJsonObject* trace, const std::function<void(const C2EventObserver::Event&)>& accepted)
{
  bool started = false, loaded = false, ended = false;
  QJsonArray events;
  const auto reject = [&](std::size_t index, const char* reason) {
    if (trace) {
      const auto& event = source[index];
      c2TraceRejection(*trace, qint64(index), reason, {{"kind", int(event.kind)}, {"entry", event.playlistId},
        {"reason", event.reason}, {"error", event.error}, {"insert_id", event.insertId}, {"insert_count", event.insertCount}});
    }
    return false;
  };
  if (trace) { trace->insert("offset", qint64(offset)); trace->insert("event_count", qint64(source.size())); }
  for (std::size_t index = offset; index < source.size(); ++index) {
    const auto& event = source[index];
    if (trace && (event.kind == MPV_EVENT_START_FILE || event.kind == MPV_EVENT_FILE_LOADED ||
                  event.kind == MPV_EVENT_END_FILE || event.kind == MPV_EVENT_QUEUE_OVERFLOW))
      c2TraceEvent(*trace, events, {{"index", qint64(index)}, {"kind", int(event.kind)},
        {"entry", event.playlistId}, {"reason", event.reason}, {"error", event.error},
        {"insert_id", event.insertId}, {"insert_count", event.insertCount}});
    if (event.kind == MPV_EVENT_QUEUE_OVERFLOW) return reject(index, "queue-overflow");
    if (event.kind == MPV_EVENT_START_FILE) {
      if (started || event.playlistId != wanted || event.error != 0) return reject(index, "unexpected-start");
      started = true;
    } else if (event.kind == MPV_EVENT_FILE_LOADED) {
      if (!started || loaded || ended || event.error != 0) return reject(index, "unexpected-loaded");
      loaded = true;
    } else if (event.kind == MPV_EVENT_END_FILE) {
      if (!started || !loaded || ended || event.playlistId != wanted || event.reason != MPV_END_FILE_REASON_STOP ||
          event.error != 0 || event.insertId != 0 || event.insertCount != 0) return reject(index, "unexpected-end");
      ended = true;
      accepted(event);
    }
  }
  const bool valid = started && loaded && ended;
  if (trace) { trace->insert("accepted", valid); if (!valid) trace->insert("rejection", "incomplete-sequence"); }
  return valid;
}
