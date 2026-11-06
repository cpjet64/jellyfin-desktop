#pragma once
#include "PlaybackNativeReceipts.h"
#include <cstdint>

namespace HealthyRestart {
struct RestoreCleanupState {
  enum class Status { Unstarted, Pending, NoLoad, Stopped, Failed };
  PlaybackNativeReceipts::Key key;
  std::uint64_t epoch = 0, request = 0, sequence = 0;
  std::int64_t entry = 0, startedEntry = 0, endEntry = 0, deadlineNs = 0, lastNs = -1;
  Status status = Status::Unstarted;
  bool claimed = false, polling = false, stopIssued = false, stopKnown = false, stopSubmitted = false;
  bool stopReply = false, stopOk = false;
  bool endSeen = false;
  bool noLoadCandidate = false;
  bool noStartTerminalObserved = false;
  int stopError = 0, replyError = 0, endReason = 0;
  struct Property { bool requested = false, known = false, submitted = false, reply = false, valid = false; std::int64_t value = -1; };
  Property idle, position;
};
}
