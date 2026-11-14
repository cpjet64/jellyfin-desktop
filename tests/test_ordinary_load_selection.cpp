#include "player/OrdinaryLoadSelection.h"
#include "player/OrdinaryLoadLedger.h"
#include "player/OrdinaryReplacementEnd.h"
#include "player/OrdinaryPlaylist.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

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

static void failedLoadDoesNotPoisonNextUnambiguousLoad()
{
  OrdinaryLoadLedger ledger;
  CHECK(ledger.admitted(100));
  CHECK(!ledger.available());
  ledger.reply(100, false); // Failed before START.
  CHECK(ledger.available());
  CHECK(ledger.admitted(101));
  CHECK(ledger.started());
  ledger.reply(101, true);
  CHECK(ledger.available());
  CHECK(ledger.admitted(102));
  CHECK(ledger.started());
  ledger.reply(102, false); // Failure after START also settles its record.
  CHECK(ledger.available());
}

static void ambiguousAndOverflowLedgersFailClosed()
{
  OrdinaryLoadLedger ledger;
  CHECK(ledger.admitted(100)); CHECK(ledger.admitted(101));
  CHECK(!ledger.started()); CHECK(!ledger.available());
  CHECK(!ledger.admitted(102));
  ledger.clear();
  for (std::uint64_t id = 1; id <= 64; ++id) CHECK(ledger.admitted(id));
  CHECK(!ledger.admitted(65)); CHECK(!ledger.available());
  ledger.clear(); ledger.failClosed();
  CHECK(!ledger.available()); CHECK(!ledger.admitted(200));
  OrdinaryLoadLedger duplicate;
  CHECK(duplicate.admitted(300));
  duplicate.reply(999, false); // An unrelated reply cannot settle this load.
  CHECK(!duplicate.available());
  duplicate.reply(300, true); duplicate.reply(300, true);
  CHECK(!duplicate.available()); CHECK(!duplicate.admitted(301));
}

static void oldReplacementEndIsScopedAndOneShot()
{
  for (std::uint64_t start : {10ULL, 11ULL}) {
    OrdinaryReplacementEnd end;
    CHECK(end.arm(5, 10, 2, 3, 100));
    CHECK(end.consume(5, true, start, 2, 3, 99));
    CHECK(!end.consume(5, true, start, 2, 3, 99));
  }
  for (int failure = 0; failure != 6; ++failure) {
    OrdinaryReplacementEnd end; CHECK(end.arm(5, 10, 2, 3, 100));
    CHECK(!end.consume(failure == 0 ? 6 : 5, failure != 1, failure == 2 ? 12 : 11,
                       failure == 3 ? 4 : 2, failure == 4 ? 4 : 3, failure == 5 ? 100 : 99));
  }
  OrdinaryReplacementEnd stopped; CHECK(stopped.arm(5, 10, 2, 3, 100));
  stopped.revoke(); CHECK(!stopped.consume(5, true, 10, 2, 3, 1));
}

struct NativeEntry {
  char idName[3] = "id", playingName[8] = "playing", filenameName[9] = "filename";
  char* keys[3] = {idName, playingName, filenameName};
  mpv_node fields[3]{};
  mpv_node_list map{};
  mpv_node node{};
  NativeEntry(std::int64_t id, bool playing, char* filename)
  {
    fields[0].format = MPV_FORMAT_INT64; fields[0].u.int64 = id;
    fields[1].format = MPV_FORMAT_FLAG; fields[1].u.flag = playing;
    fields[2].format = MPV_FORMAT_STRING; fields[2].u.string = filename;
    map.num = 3; map.keys = keys; map.values = fields;
    node.format = MPV_FORMAT_NODE_MAP; node.u.list = &map;
  }
};

static void nativePlaylistMetadataRequiresUniqueCurrentExactUrl()
{
  char url[] = "https://example.invalid/media?synthetic=1";
  char wrong[] = "https://example.invalid/other";
  NativeEntry first(5, false, url), second(20, true, url);
  mpv_node values[] = {first.node, second.node};
  mpv_node_list list{}; list.num = 2; list.values = values;
  mpv_node playlist{}; playlist.format = MPV_FORMAT_NODE_ARRAY; playlist.u.list = &list;
  QSet<std::int64_t> ids;
  CHECK(OrdinaryPlaylist::entries(playlist, ids));
  CHECK(ids.size() == 2 && ids.contains(5) && ids.contains(20));
  CHECK(OrdinaryPlaylist::currentMatches(playlist, 20, QString::fromUtf8(url)));
  CHECK(!OrdinaryPlaylist::currentMatches(playlist, 5, QString::fromUtf8(url)));
  CHECK(!OrdinaryPlaylist::currentMatches(playlist, 20, QString::fromUtf8(wrong)));
  first.fields[1].u.flag = 1;
  CHECK(!OrdinaryPlaylist::currentMatches(playlist, 20, QString::fromUtf8(url)));
  first.fields[1].u.flag = 0;
  first.fields[0].u.int64 = 20;
  CHECK(!OrdinaryPlaylist::entries(playlist, ids));
  CHECK(ids.isEmpty());
  CHECK(!OrdinaryPlaylist::currentMatches(playlist, 20, QString::fromUtf8(url)));
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
  failedLoadDoesNotPoisonNextUnambiguousLoad();
  ambiguousAndOverflowLedgersFailClosed();
  oldReplacementEndIsScopedAndOneShot();
  nativePlaylistMetadataRequiresUniqueCurrentExactUrl();
}
