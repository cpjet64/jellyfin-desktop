#include "PlaybackNativeReceipts.h"
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " << #x << '\n'; std::exit(1); } } while (false)
using Ledger = PlaybackNativeReceipts;
using Key = Ledger::Key;
constexpr Key current{2, 3, 4, 5};

static void prepare(Ledger& ledger)
{
  CHECK(ledger.begin(current));
  CHECK(ledger.consume(current));
  ledger.submitted(0);
  CHECK(ledger.snapshot().submissionObserved && ledger.snapshot().submitted);
  CHECK(!ledger.snapshot().reply && !ledger.snapshot().started && !ledger.snapshot().settled);
}

static void bothEventOrders()
{
  Ledger first;
  prepare(first);
  first.reply(5, true, 19);
  CHECK(first.snapshot().replyOk && !first.snapshot().started && !first.snapshot().settled);
  first.start(19);
  CHECK(first.snapshot().started && !first.snapshot().invalid);
  Ledger second;
  prepare(second);
  second.start(19);
  CHECK(!second.snapshot().started);
  second.reply(5, true, 19);
  CHECK(second.snapshot().started && second.snapshot().entry == 19);
}

static void failedSubmissionAndReply()
{
  Ledger immediate;
  CHECK(immediate.begin(current));
  CHECK(immediate.consume(current));
  immediate.submitted(-1);
  CHECK(immediate.snapshot().submissionObserved && immediate.snapshot().invalid &&
        immediate.snapshot().settled && !immediate.snapshot().submitted);
  immediate.reply(5, true, 19);
  CHECK(!immediate.snapshot().replyOk);
  Ledger asynchronous;
  prepare(asynchronous);
  asynchronous.reply(5, false, 0);
  CHECK(asynchronous.snapshot().reply && asynchronous.snapshot().invalid &&
        asynchronous.snapshot().settled && !asynchronous.snapshot().replyOk);
}

static void oneUseAndIdentity()
{
  Ledger invalid;
  CHECK((!invalid.begin(Key{0, 3, 4, 5})));
  CHECK((!invalid.begin(Key{2, 0, 4, 5})));
  CHECK((!invalid.begin(Key{2, 3, 0, 5})));
  CHECK((!invalid.begin(Key{2, 3, 4, 0})));
  Ledger ledger;
  CHECK(ledger.begin(current));
  CHECK(!ledger.begin(current));
  CHECK((!ledger.consume(Key{1, 3, 4, 5})));
  CHECK((!ledger.consume(Key{2, 3, 4, 6})));
  CHECK((!ledger.consume(Key{2, 3, 5, 5})));
  CHECK((!ledger.consume(Key{2, 4, 4, 5})));
  CHECK(ledger.consume(current));
  CHECK(!ledger.consume(current));
  ledger.submitted(0);
  ledger.reply(6, true, 20);
  CHECK(!ledger.snapshot().reply);
  ledger.reply(5, true, 19);
  CHECK(ledger.snapshot().entry == 19);
  ledger.reply(5, true, 19);
  CHECK(ledger.snapshot().invalid);
}

static void invalidationAndOldAttempt()
{
  Ledger old;
  prepare(old);
  old.invalidate();
  old.reply(5, true, 19);
  CHECK(old.snapshot().invalid && old.snapshot().replyOk && !old.snapshot().settled);
  Ledger next;
  CHECK((next.begin(Key{8, 4, 6, 7})));
  CHECK((next.consume(Key{8, 4, 6, 7})));
  next.submitted(0);
  next.reply(5, true, 19);
  CHECK(!next.snapshot().reply && next.snapshot().key.attempt == 7);
  next.reply(7, true, 21);
  next.start(19);
  CHECK(next.snapshot().invalid && !next.snapshot().started && next.snapshot().entry == 21);
}

static void invalidatedAttemptsStillSettle()
{
  Ledger late;
  prepare(late);
  late.invalidate();
  late.reply(5, true, 19);
  late.start(19);
  late.end(20, false);
  CHECK(late.snapshot().invalid && !late.snapshot().started && !late.snapshot().settled);
  late.end(19, false);
  CHECK(late.snapshot().invalid && late.snapshot().ended && late.snapshot().settled);

  Ledger endFirst;
  prepare(endFirst);
  endFirst.invalidate();
  endFirst.end(19, false);
  CHECK(!endFirst.snapshot().settled);
  endFirst.reply(5, true, 19);
  CHECK(endFirst.snapshot().invalid && endFirst.snapshot().settled);

  Ledger failure;
  prepare(failure);
  failure.invalidate();
  failure.reply(5, false, 0);
  CHECK(failure.snapshot().invalid && failure.snapshot().reply && failure.snapshot().settled);
}

static void reentrantSubmissionAndConflictSettlement()
{
  for (int result : {0, -1}) {
    Ledger ledger;
    CHECK(ledger.begin(current));
    CHECK(ledger.consume(current));
    ledger.invalidate();
    ledger.submitted(result);
    CHECK(ledger.snapshot().invalid && ledger.snapshot().submissionObserved);
    CHECK(ledger.snapshot().submitted == (result == 0));
    CHECK(ledger.snapshot().settled == (result < 0));
  }
  Ledger conflict;
  prepare(conflict);
  conflict.start(20);
  conflict.end(19, false);
  conflict.invalidate();
  conflict.reply(5, true, 19);
  CHECK(conflict.snapshot().invalid && conflict.snapshot().ended && conflict.snapshot().settled);
}

static void malformedDuplicatesAndRedirect()
{
  Ledger missing;
  prepare(missing);
  missing.reply(5, true, 0);
  CHECK(missing.snapshot().invalid);
  Ledger badEvent;
  prepare(badEvent);
  badEvent.start(-1);
  CHECK(badEvent.snapshot().invalid);
  Ledger duplicate;
  prepare(duplicate);
  duplicate.start(19);
  duplicate.start(19);
  CHECK(duplicate.snapshot().invalid);
  Ledger redirected;
  prepare(redirected);
  redirected.end(19, true);
  redirected.reply(5, true, 19);
  CHECK(redirected.snapshot().invalid);
  Ledger conflict;
  prepare(conflict);
  conflict.start(20);
  conflict.reply(5, true, 19);
  CHECK(conflict.snapshot().invalid && !conflict.snapshot().started);
}

static void boundAndEnd()
{
  Ledger bounded;
  prepare(bounded);
  for (int i = 1; i <= 16; ++i) bounded.start(i);
  CHECK(!bounded.snapshot().invalid);
  bounded.start(17);
  CHECK(bounded.snapshot().invalid);
  Ledger ended;
  prepare(ended);
  ended.end(19, false);
  ended.reply(5, true, 19);
  CHECK(ended.snapshot().ended && ended.snapshot().settled && !ended.snapshot().started);
}

int main()
{
  bothEventOrders();
  failedSubmissionAndReply();
  oneUseAndIdentity();
  invalidationAndOldAttempt();
  invalidatedAttemptsStillSettle();
  reentrantSubmissionAndConflictSettlement();
  malformedDuplicatesAndRedirect();
  boundAndEnd();
}
