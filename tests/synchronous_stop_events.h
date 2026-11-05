#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

enum class StopEventKind { Other, Start, End, Overflow };
struct StopEvent { StopEventKind kind; std::int64_t entry; };
enum class StopEventState { Settled, Pending, Invalid };

// The boundary is captured after A is observed and immediately before B.
// An end for A is allowed; post-boundary starts are not named as B by inference.
inline StopEventState classifyStopEvents(const std::vector<StopEvent>& events, std::size_t boundary,
                                        std::int64_t aEntry)
{
  if (aEntry <= 0 || boundary > events.size() || events.size() > 4096) return StopEventState::Invalid;
  bool startedA = false;
  for (std::size_t i = 0; i < boundary; ++i) {
    const auto& event = events[i];
    if (event.kind == StopEventKind::Overflow) return StopEventState::Invalid;
    if (event.entry != aEntry) continue;
    if (event.kind == StopEventKind::End || (event.kind == StopEventKind::Start && startedA))
      return StopEventState::Invalid;
    if (event.kind == StopEventKind::Start) startedA = true;
  }
  if (!startedA) return StopEventState::Invalid;
  std::map<std::int64_t, bool> starts;
  bool endedA = false;
  for (std::size_t i = boundary; i < events.size(); ++i) {
    const auto& event = events[i];
    if (event.kind == StopEventKind::Overflow) return StopEventState::Invalid;
    if (event.kind == StopEventKind::Start) {
      if (event.entry <= 0 || event.entry == aEntry || !starts.emplace(event.entry, false).second)
        return StopEventState::Invalid;
    } else if (event.kind == StopEventKind::End) {
      if (event.entry == aEntry && !endedA) endedA = true;
      else {
        const auto found = starts.find(event.entry);
        if (found == starts.end() || found->second) return StopEventState::Invalid;
        found->second = true;
      }
    }
  }
  for (const auto& start : starts) if (!start.second) return StopEventState::Pending;
  return StopEventState::Settled;
}
