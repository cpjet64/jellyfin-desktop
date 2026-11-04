#pragma once

#include <mpv/client.h>
#include <cstdint>
#include <vector>

struct C2TerminalEvent
{
  mpv_event_id kind;
  int64_t entry = 0;
  int reason = 0;
  int error = 0;
  int64_t insertId = 0;
  int insertCount = 0;
};

inline bool c2ErrorEntry(const std::vector<C2TerminalEvent>& events, int64_t wanted)
{
  if (wanted <= 0) return false;
  bool started = false, loaded = false, ended = false;
  for (const auto& event : events) {
    if (event.kind == MPV_EVENT_QUEUE_OVERFLOW) return false;
    if (event.kind == MPV_EVENT_START_FILE) {
      if (started || event.entry != wanted) return false;
      started = true;
    } else if (event.kind == MPV_EVENT_FILE_LOADED) {
      if (!started || loaded || ended || event.error != 0) return false;
      loaded = true;
    } else if (event.kind == MPV_EVENT_END_FILE) {
      if (!loaded || ended || event.entry != wanted || event.reason != MPV_END_FILE_REASON_ERROR ||
          event.error >= 0 || event.insertId != 0 || event.insertCount != 0) return false;
      ended = true;
    }
  }
  return started && loaded && ended;
}

inline bool c2ErrorCleanupEligible(bool retired, bool sameBinding,
                                   const std::vector<C2TerminalEvent>& events, int64_t entry)
{
  return retired && sameBinding && c2ErrorEntry(events, entry);
}

inline bool c2ErrorTerminalProperties(int idleStatus, int idle, int voStatus,
                                      int forceStatus, bool forceNo)
{
  return idleStatus == 0 && idle == 1 && voStatus == MPV_ERROR_PROPERTY_UNAVAILABLE &&
    forceStatus == 0 && forceNo;
}
