#include "async_stop.h"
#include <utility>

namespace HealthyRestart {
bool AsyncStop::begin(Key key, std::int64_t deadline, Clock clock) noexcept
{
  constexpr auto maxSafe = std::uint64_t{9007199254740991ULL};
  if (facts_.admissionClosed || !key.host || !key.core || !key.epoch || !key.request ||
      !key.revision || !key.attempt || key.attempt > maxSafe || key.entry < 0 ||
      key.origin != Origin::InternalReplacement) return false;
  facts_.key = key;
  facts_.admissionClosed = true;
  deadline_ = deadline;
  try { clock_ = std::move(clock); } catch (...) { facts_.failed = true; return false; }
  return timeValid();
}

bool AsyncStop::timeValid() const noexcept
{
  if (!facts_.admissionClosed || facts_.failed || facts_.cancelled) return false;
  try {
    if (clock_) {
      const auto now = clock_();
      if (now >= 0 && now >= lastTime_ && now < deadline_) { lastTime_ = now; return true; }
    }
  } catch (...) {}
  facts_.failed = true;
  return false;
}

bool AsyncStop::matches(Key key) const noexcept
{
  const auto& a = facts_.key;
  return timeValid() && a.host == key.host && a.core == key.core && a.epoch == key.epoch &&
      a.request == key.request && a.revision == key.revision && a.attempt == key.attempt &&
      a.entry == key.entry && a.paused == key.paused && a.origin == key.origin;
}

bool AsyncStop::requestQuery(Query query) noexcept
{
  if (!timeValid()) return false;
  const auto i = index(query);
  if (i >= facts_.queries.size() || facts_.queries[i].requested) return false;
  if (query == Query::AfterIdle ? !readyForIdleQuery() : (facts_.key.entry != 0 || facts_.stopRequested)) return false;
  facts_.queries[i].requested = true;
  return true;
}

void AsyncStop::querySubmitted(Query query, int result) noexcept
{
  const auto i = index(query);
  if (i >= facts_.queries.size()) { fail(); return; }
  auto& q = facts_.queries[i];
  if (!q.requested || q.submissionKnown) { fail(); return; }
  q.submissionKnown = true; q.submissionError = result; q.submitted = result >= 0;
  if (!q.submitted || !timeValid()) fail();
}

void AsyncStop::queryReply(Query query, int error, bool valid, std::int64_t value) noexcept
{
  const auto i = index(query);
  if (i >= facts_.queries.size()) { fail(); return; }
  auto& q = facts_.queries[i];
  if (!timeValid() || !q.requested || q.replied) { fail(); return; }
  q.replied = true; q.replyError = error; q.value = value; q.valid = valid && error >= 0;
  const auto expected = query == Query::BeforePosition ? -1 : 1;
  if (!q.valid || value != expected) fail();
}

bool AsyncStop::queryGood(Query query, std::int64_t value) const noexcept
{
  const auto& q = facts_.queries[index(query)];
  return q.requested && q.submissionKnown && q.submitted && q.replied && q.valid && q.value == value;
}

bool AsyncStop::readyToSubmitStop() const noexcept
{
  return timeValid() && !facts_.stopRequested &&
      (facts_.key.entry > 0 || (queryGood(Query::BeforeIdle, 1) && queryGood(Query::BeforePosition, -1)));
}
bool AsyncStop::requestStop() noexcept
{
  if (!readyToSubmitStop()) return false;
  facts_.stopRequested = true;
  return true;
}
void AsyncStop::stopSubmitted(int result) noexcept
{
  if (!facts_.stopRequested || facts_.submissionKnown) { fail(); return; }
  facts_.submissionKnown = true; facts_.submissionError = result; facts_.submitted = result >= 0;
  if (!facts_.submitted || !timeValid()) fail();
}
void AsyncStop::stopReply(int error) noexcept
{
  if (!timeValid() || !facts_.stopRequested || facts_.replied) { fail(); return; }
  facts_.replied = true; facts_.replyError = error; facts_.replyOk = error >= 0;
  if (!facts_.replyOk) fail();
}
bool AsyncStop::endFile(std::int64_t entry, bool stopReason) noexcept
{
  if (!timeValid() || !facts_.stopRequested || facts_.endSeen ||
      facts_.key.entry <= 0 || entry != facts_.key.entry || !stopReason) { fail(); return false; }
  facts_.endSeen = true;
  return true;
}
bool AsyncStop::readyForIdleQuery() const noexcept
{
  return timeValid() && facts_.stopRequested && facts_.submissionKnown && facts_.submitted && facts_.replied &&
      facts_.replyOk && !facts_.failed && !facts_.cancelled && (facts_.key.entry == 0 || facts_.endSeen);
}
void AsyncStop::fail() noexcept { if (facts_.admissionClosed) facts_.failed = true; }
void AsyncStop::cancel() noexcept { if (facts_.admissionClosed) { facts_.cancelled = true; facts_.failed = true; } }
AsyncStop::Status AsyncStop::status() const noexcept
{
  if (!facts_.admissionClosed) return Status::Unstarted;
  if (!timeValid()) return Status::Failed;
  return readyForIdleQuery() && queryGood(Query::AfterIdle, 1) ? Status::Complete : Status::Pending;
}
}
