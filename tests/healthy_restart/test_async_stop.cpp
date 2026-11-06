#include "async_stop.h"
#include <cstdlib>
using HealthyRestart::AsyncStop;
#define CHECK(value) do { if (!(value)) std::abort(); } while (false)

namespace {
std::int64_t now = 1;
AsyncStop::Key key(std::int64_t entry = 42)
{ return {1, 2, 3, 4, 5, 6, entry, true, AsyncStop::Origin::InternalReplacement}; }
auto clock() { return [] { return now; }; }
void query(AsyncStop& s, AsyncStop::Query q, std::int64_t value)
{
  CHECK(s.requestQuery(q));
  s.queryReply(q, 0, true, value); // Reentrant delivery before the submission returns.
  s.querySubmitted(q, 0);
}
void activeEndBeforeReply()
{
  now = 1; AsyncStop s; CHECK(s.begin(key(), 10, clock()));
  CHECK(s.requestStop());
  CHECK(s.endFile(42, true));
  s.stopReply(0); s.stopSubmitted(0);
  CHECK(s.readyForIdleQuery());
  query(s, AsyncStop::Query::AfterIdle, 1);
  CHECK(s.status() == AsyncStop::Status::Complete && s.facts().key.paused);
}
void activeReplyBeforeEnd()
{
  now = 1; AsyncStop s; CHECK(s.begin(key(), 10, clock()));
  CHECK(s.requestStop()); s.stopSubmitted(0); s.stopReply(0);
  CHECK(!s.readyForIdleQuery()); CHECK(s.endFile(42, true));
  query(s, AsyncStop::Query::AfterIdle, 1);
  CHECK(s.status() == AsyncStop::Status::Complete);
}
void noEntryRequiresFreshEvidence()
{
  now = 1; AsyncStop s; CHECK(s.begin(key(0), 10, clock()));
  CHECK(!s.requestStop());
  query(s, AsyncStop::Query::BeforeIdle, 1);
  CHECK(!s.requestStop());
  query(s, AsyncStop::Query::BeforePosition, -1);
  CHECK(s.requestStop()); s.stopSubmitted(0); s.stopReply(0);
  CHECK(s.readyForIdleQuery()); query(s, AsyncStop::Query::AfterIdle, 1);
  CHECK(s.status() == AsyncStop::Status::Complete);
}
void failuresAndDuplicates()
{
  now = 1; AsyncStop s; CHECK(s.begin(key(), 10, clock())); CHECK(s.requestStop());
  s.stopReply(-8); s.stopSubmitted(0);
  CHECK(s.status() == AsyncStop::Status::Failed && s.facts().replyError == -8 && s.facts().submissionKnown);
  AsyncStop duplicate; CHECK(duplicate.begin(key(), 10, clock())); CHECK(duplicate.requestStop());
  duplicate.stopSubmitted(0); duplicate.stopReply(0); duplicate.stopReply(0);
  CHECK(duplicate.status() == AsyncStop::Status::Failed);
  AsyncStop badEntry; CHECK(badEntry.begin(key(), 10, clock())); CHECK(badEntry.requestStop());
  CHECK(!badEntry.endFile(43, true) && badEntry.status() == AsyncStop::Status::Failed);
  AsyncStop rejected; CHECK(rejected.begin(key(), 10, clock())); CHECK(rejected.requestStop());
  rejected.stopSubmitted(-12); CHECK(rejected.status() == AsyncStop::Status::Failed && rejected.facts().submissionError == -12);
}
void identityCancellationAndDeadline()
{
  now = 1; AsyncStop s; CHECK(s.begin(key(), 10, clock()));
  auto other = key(); ++other.core; CHECK(!s.matches(other) && s.matches(key()));
  CHECK(s.requestStop()); s.cancel(); s.stopReply(0); s.stopSubmitted(0);
  CHECK(s.status() == AsyncStop::Status::Failed && s.facts().submissionKnown);
  AsyncStop expired; CHECK(expired.begin(key(), 10, clock())); CHECK(expired.requestStop());
  now = 10; expired.stopReply(0); expired.stopSubmitted(0);
  CHECK(expired.status() == AsyncStop::Status::Failed);
}
void idleFailureAndClockRegression()
{
  now = 2; AsyncStop s; CHECK(s.begin(key(0), 10, clock()));
  CHECK(s.requestQuery(AsyncStop::Query::BeforeIdle));
  s.queryReply(AsyncStop::Query::BeforeIdle, -7, false, 0);
  s.querySubmitted(AsyncStop::Query::BeforeIdle, 0);
  const auto q = s.facts().queries[static_cast<std::size_t>(AsyncStop::Query::BeforeIdle)];
  CHECK(s.status() == AsyncStop::Status::Failed && q.submissionKnown && q.replyError == -7);
  AsyncStop backward; CHECK(backward.begin(key(), 10, clock()));
  now = 1; CHECK(backward.status() == AsyncStop::Status::Failed);
  now = 3; CHECK(backward.status() == AsyncStop::Status::Failed);
  AsyncStop notIdle; CHECK(notIdle.begin(key(0), 10, clock()));
  CHECK(notIdle.requestQuery(AsyncStop::Query::BeforeIdle));
  notIdle.querySubmitted(AsyncStop::Query::BeforeIdle, 0);
  notIdle.queryReply(AsyncStop::Query::BeforeIdle, 0, true, 0);
  CHECK(notIdle.status() == AsyncStop::Status::Failed && !notIdle.requestStop());
}
}
int main()
{
  activeEndBeforeReply(); activeReplyBeforeEnd(); noEntryRequiresFreshEvidence();
  failuresAndDuplicates(); identityCancellationAndDeadline();
  idleFailureAndClockRegression();
}
