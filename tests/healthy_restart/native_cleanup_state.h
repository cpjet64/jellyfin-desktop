#pragma once
#include "PlaybackNativeReceipts.h"
#include "failure_cause.h"
#include <cstdint>
#include <optional>

namespace HealthyRestart {
struct VerifiedCleanupCancel { std::uint64_t sequence = 0, revision = 0; };
struct RestoreCleanupState {
  enum class Status { Unstarted, Pending, NoLoad, Stopped, Failed };
  enum class Origin { None, AuthenticatedCancel, InternalFailure };
  PlaybackNativeReceipts::Key key;
  std::uint64_t epoch = 0, request = 0;
  std::optional<VerifiedCleanupCancel> cancel;
  std::int64_t entry = 0, startedEntry = 0, endEntry = 0, deadlineNs = 0, lastNs = -1;
  Status status = Status::Unstarted;
  Origin origin = Origin::None;
  FailureCause firstFailure = FailureCause::None;
  bool claimed = false, polling = false, stopAdmitted = false, stopIssued = false, stopKnown = false, stopSubmitted = false;
  bool stopReply = false, stopOk = false;
  bool endSeen = false;
  bool noLoadCandidate = false;
  bool noStartTerminalObserved = false;
  int stopError = 0, replyError = 0, endReason = 0;
  struct Property { bool requested = false, known = false, submitted = false, reply = false, valid = false; std::int64_t value = -1; };
  Property idle, position;

  bool propertiesReady() const noexcept
  {
    return idle.known && idle.submitted && idle.reply && idle.valid && idle.value == 1 &&
        position.known && position.submitted && position.reply && position.valid && position.value == -1;
  }

  bool terminalReceiptMatches(const PlaybackNativeReceipts::Snapshot& receipt, bool started) const noexcept
  {
    const bool terminal = started ? receipt.started && receipt.ended && receipt.settled && endSeen && endEntry == entry :
        !receipt.started && !receipt.ended && !startedEntry && !endSeen;
    return !receipt.invalid && receipt.replyOk && terminal && receipt.entry == entry &&
        receipt.key.host == key.host && receipt.key.core == key.core && receipt.key.revision == key.revision &&
        receipt.key.attempt == key.attempt;
  }
};
}
