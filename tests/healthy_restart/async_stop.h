#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <optional>

namespace HealthyRestart {
// Control-thread ledger shared by the copied Player and its fixture adapter.
// Native events must be decoded and matched to the held Player before entry.
class AsyncStop {
public:
  enum class Status { Unstarted, Pending, Complete, Failed };
  enum class Query { BeforeIdle, BeforePosition, AfterIdle };
  enum class Origin { Invalid, InternalReplacement };
  struct Key {
    std::uint64_t host = 0, core = 0, epoch = 0, request = 0, revision = 0, attempt = 0;
    std::int64_t entry = 0;
    bool paused = false;
    Origin origin = Origin::Invalid;
  };
  struct QueryFacts {
    bool requested = false, submissionKnown = false, submitted = false, replied = false, valid = false;
    int submissionError = 0, replyError = 0;
    std::int64_t value = 0;
  };
  struct Facts {
    Key key;
    bool admissionClosed = false, stopRequested = false, submissionKnown = false, submitted = false;
    bool replied = false, replyOk = false, endSeen = false, failed = false, cancelled = false;
    int submissionError = 0, replyError = 0;
    std::array<QueryFacts, 3> queries{};
  };
  using Clock = std::function<std::int64_t()>;
  bool begin(Key key, std::int64_t deadline, Clock clock) noexcept;
  bool requestQuery(Query query) noexcept;
  void querySubmitted(Query query, int result) noexcept;
  void queryReply(Query query, int error, bool valid, std::int64_t value) noexcept;
  bool readyToSubmitStop() const noexcept;
  bool requestStop() noexcept;
  void stopSubmitted(int result) noexcept;
  void stopReply(int error) noexcept;
  // True only for the single matching STOP termination of this active entry.
  bool endFile(std::int64_t entry, bool stopReason) noexcept;
  bool readyForIdleQuery() const noexcept;
  void fail() noexcept;
  void cancel() noexcept;
  bool matches(Key key) const noexcept;
  Status status() const noexcept;
  Facts facts() const noexcept { timeValid(); return facts_; }
private:
  static std::size_t index(Query query) noexcept { return static_cast<std::size_t>(query); }
  bool queryGood(Query query, std::int64_t value) const noexcept;
  bool timeValid() const noexcept;
  mutable Facts facts_;
  Clock clock_;
  std::int64_t deadline_ = 0;
  mutable std::int64_t lastTime_ = 0;
};
}
