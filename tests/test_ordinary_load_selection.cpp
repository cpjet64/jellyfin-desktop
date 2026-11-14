#include "player/OrdinaryLoadSelection.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #x); std::abort(); } } while (false)

static bool begin(OrdinaryLoadSelection& selection)
{
  return selection.begin(100, 2, 3, 10, {5, 6}, 7, 8);
}

static void entrylessSuccessPreservesChoicesInEitherEventOrder()
{
  for (int order = 0; order != 2; ++order) {
    OrdinaryLoadSelection selection; QVariant audio, subtitle;
    CHECK(begin(selection));
    if (order == 0) {
      selection.reply(100, true);
      selection.started(20, 11);
    } else {
      selection.started(20, 11);
      selection.reply(100, true);
    }
    selection.after(20, 11, true);
    CHECK(selection.select(2, 3, 20, 11, false, audio, subtitle));
    CHECK(audio.toInt() == 7 && subtitle.toInt() == 8);
    CHECK(!selection.select(2, 3, 20, 11, true, audio, subtitle));
    selection.loaded(20, 11);
    CHECK(selection.select(2, 3, 20, 11, true, audio, subtitle));
  }
}

static void preloadBeforeReplyWaitsForOwnedReplyAndLoadedEvent()
{
  OrdinaryLoadSelection selection; QVariant audio, subtitle;
  CHECK(begin(selection));
  selection.started(20, 11);
  CHECK(!selection.select(2, 3, 20, 11, false, audio, subtitle));
  selection.after(20, 11, true);
  selection.loaded(20, 11);
  selection.reply(100, true);
  CHECK(selection.select(2, 3, 20, 11, true, audio, subtitle));
}

static void wrongOrFailedReplyDoesNotAuthorizeChoices()
{
  for (int failure = 0; failure != 2; ++failure) {
    OrdinaryLoadSelection selection; QVariant audio, subtitle;
    CHECK(begin(selection)); selection.started(20, 11); selection.after(20, 11, true);
    selection.reply(failure == 0 ? 101 : 100, failure == 0);
    CHECK(!selection.select(2, 3, 20, 11, false, audio, subtitle));
  }
}

static void duplicateReplyOrWrongLoadedEntryRevokesSelection()
{
  QVariant audio, subtitle;
  OrdinaryLoadSelection duplicate;
  CHECK(begin(duplicate)); duplicate.reply(100, true); duplicate.reply(100, true);
  duplicate.started(20, 11); duplicate.after(20, 11, true);
  CHECK(!duplicate.select(2, 3, 20, 11, false, audio, subtitle));
  OrdinaryLoadSelection mismatch;
  CHECK(begin(mismatch)); mismatch.reply(100, true); mismatch.started(20, 11); mismatch.after(20, 11, true);
  mismatch.loaded(21, 11);
  CHECK(!mismatch.select(2, 3, 20, 11, false, audio, subtitle));
}

static void staleScopeAndConflictingStartCannotApplyChoices()
{
  OrdinaryLoadSelection selection; QVariant audio, subtitle;
  CHECK(begin(selection)); selection.reply(100, true); selection.started(20, 11); selection.after(20, 11, true);
  CHECK(!selection.select(3, 3, 20, 11, false, audio, subtitle));
  CHECK(!selection.select(2, 4, 20, 11, false, audio, subtitle));
  CHECK(!selection.select(2, 3, 21, 11, false, audio, subtitle));
  CHECK(!selection.select(2, 3, 20, 12, false, audio, subtitle));
  selection.started(21, 12);
  CHECK(!selection.select(2, 3, 20, 11, false, audio, subtitle));
  CHECK(!selection.select(2, 3, 21, 12, false, audio, subtitle));
}

static void stopOrCompetingSubmissionRevokesOldSelection()
{
  OrdinaryLoadSelection stopped; QVariant audio, subtitle;
  CHECK(begin(stopped)); stopped.reply(100, true); stopped.started(20, 11); stopped.after(20, 11, true);
  stopped.revoke();
  CHECK(!stopped.select(2, 3, 20, 11, false, audio, subtitle));

  OrdinaryLoadSelection competing;
  CHECK(begin(competing));
  CHECK(!competing.begin(101, 3, 3, 10, {5, 6}, 9, 10));
  competing.started(20, 11); competing.reply(100, true); competing.after(20, 11, true);
  CHECK(!competing.select(2, 3, 20, 11, false, audio, subtitle));
}

static void beforeAndAfterPlaylistProofIsRequired()
{
  QVariant audio, subtitle;
  OrdinaryLoadSelection oldEntry;
  CHECK(begin(oldEntry)); oldEntry.reply(100, true); oldEntry.started(5, 11); oldEntry.after(5, 11, true);
  CHECK(!oldEntry.select(2, 3, 5, 11, false, audio, subtitle));
  OrdinaryLoadSelection wrongAfter;
  CHECK(begin(wrongAfter)); wrongAfter.reply(100, true); wrongAfter.started(20, 11); wrongAfter.after(20, 11, false);
  CHECK(!wrongAfter.select(2, 3, 20, 11, false, audio, subtitle));
  OrdinaryLoadSelection wrongSerial;
  CHECK(begin(wrongSerial)); wrongSerial.reply(100, true); wrongSerial.started(20, 12); wrongSerial.after(20, 12, true);
  CHECK(!wrongSerial.select(2, 3, 20, 12, false, audio, subtitle));
}

static void onlyKnownPreviousEndCanPrecedeSelection()
{
  QVariant audio, subtitle;
  OrdinaryLoadSelection known;
  CHECK(begin(known)); known.ended(5); known.started(20, 11); known.reply(100, true); known.after(20, 11, true);
  CHECK(known.select(2, 3, 20, 11, false, audio, subtitle));
  OrdinaryLoadSelection unknown;
  CHECK(begin(unknown)); unknown.ended(99); unknown.started(20, 11); unknown.reply(100, true); unknown.after(20, 11, true);
  CHECK(!unknown.select(2, 3, 20, 11, false, audio, subtitle));
}

int main()
{
  entrylessSuccessPreservesChoicesInEitherEventOrder();
  preloadBeforeReplyWaitsForOwnedReplyAndLoadedEvent();
  wrongOrFailedReplyDoesNotAuthorizeChoices();
  duplicateReplyOrWrongLoadedEntryRevokesSelection();
  staleScopeAndConflictingStartCannotApplyChoices();
  stopOrCompetingSubmissionRevokesOldSelection();
  beforeAndAfterPlaylistProofIsRequired();
  onlyKnownPreviousEndCanPrecedeSelection();
}
