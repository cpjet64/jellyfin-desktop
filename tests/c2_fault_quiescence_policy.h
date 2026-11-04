#pragma once

#include "c2_error_terminal.h"

// The observer is a secondary mpv client. Only one known fixture entry is
// admissible, and a STOP event is not inferred from idle or from the GUI signal.
inline bool c2QuiescenceStop(const std::vector<C2TerminalEvent>& events, int64_t entry)
{
  if (entry <= 0) return false;
  bool started = false, loaded = false, stopped = false;
  for (const auto& event : events) {
    if (event.kind == MPV_EVENT_QUEUE_OVERFLOW) return false;
    if (event.kind == MPV_EVENT_START_FILE) {
      if (started || event.entry != entry) return false;
      started = true;
    } else if (event.kind == MPV_EVENT_FILE_LOADED) {
      if (!started || loaded || stopped || event.error != 0) return false;
      loaded = true;
    } else if (event.kind == MPV_EVENT_END_FILE) {
      if (!loaded || stopped || event.entry != entry || event.reason != MPV_END_FILE_REASON_STOP ||
          event.error != 0 || event.insertId != 0 || event.insertCount != 0) return false;
      stopped = true;
    }
  }
  return started && loaded && stopped;
}
