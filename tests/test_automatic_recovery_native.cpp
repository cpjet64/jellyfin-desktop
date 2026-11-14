#include "player/AutomaticRecoveryNative.h"
#include "player/AutomaticRecoverySession.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #x); std::abort(); } } while (false)

static void keyedNativeEntryOnly()
{
  char entryKey[] = "playlist_entry_id";
  char* keys[] = {entryKey};
  mpv_node value{};
  value.format = MPV_FORMAT_INT64;
  value.u.int64 = 123;
  mpv_node_list list{};
  list.num = 1;
  list.keys = keys;
  list.values = &value;
  mpv_event_command command{};
  command.result.format = MPV_FORMAT_NODE_MAP;
  command.result.u.list = &list;
  mpv_event event{};
  event.event_id = MPV_EVENT_COMMAND_REPLY;
  event.reply_userdata = (std::uint64_t{3} << 61) | 7;
  event.data = &command;
  const auto expected = event.reply_userdata;
  CHECK(AutomaticRecoveryNative::loadEntry(&event, expected) == 123);
  CHECK(AutomaticRecoveryNative::loadEntry(&event, expected - 1) == 0);
  event.error = -1;
  CHECK(AutomaticRecoveryNative::loadEntry(&event, expected) == 0);
  event.error = 0;
  value.format = MPV_FORMAT_STRING;
  CHECK(AutomaticRecoveryNative::loadEntry(&event, expected) == 0);
  value.format = MPV_FORMAT_INT64;
  value.u.int64 = 0;
  CHECK(AutomaticRecoveryNative::loadEntry(&event, expected) == 0);
  CHECK(AutomaticRecoveryNative::loadEntry(nullptr, expected) == 0);
}

static void duplicateNativeEntryIsRejected()
{
  char key[] = "playlist_entry_id";
  char* keys[] = {key, key};
  mpv_node values[2]{};
  for (auto& value : values) { value.format = MPV_FORMAT_INT64; value.u.int64 = 8; }
  mpv_node_list list{};
  list.num = 2; list.keys = keys; list.values = values;
  mpv_event_command command{};
  command.result.format = MPV_FORMAT_NODE_MAP; command.result.u.list = &list;
  mpv_event event{};
  event.event_id = MPV_EVENT_COMMAND_REPLY; event.reply_userdata = 9; event.data = &command;
  CHECK(AutomaticRecoveryNative::loadEntry(&event, 9) == 0);
}

static void snapshotRequiresMatchingAcceptedLoadedEntry()
{
  AutomaticRecoverySession session;
  const AutomaticRecoverySession::Key key{1, 2, 3};
  AutomaticRecoverySession::Snapshot result;
  session.begin(key, QStringLiteral("https://example.invalid/private"), {}, {}, 2, 3, false);
  CHECK(!session.snapshot(key, result));
  CHECK(!session.accept({1, 3, 3}, 7));
  CHECK(session.accept(key, 7));
  CHECK(!session.position(key, 7, 1000));
  CHECK(!session.loaded(key, 8));
  CHECK(session.loaded(key, 7));
  CHECK(!session.position(key, 8, 1000));
  CHECK(session.position(key, 7, 1000));
  CHECK(session.snapshot(key, result));
  CHECK(result.entry == 7 && result.positionMs == 1000 && !result.paused);
  CHECK(result.audio.toInt() == 2 && result.subtitle.toInt() == 3);
  CHECK(!session.snapshot({2, 2, 3}, result));
  session.invalidate();
  CHECK(!session.snapshot(key, result));
}

static void newerIntentNeedsFreshPosition()
{
  AutomaticRecoverySession session;
  const AutomaticRecoverySession::Key old{1, 2, 3}, next{1, 3, 3};
  AutomaticRecoverySession::Snapshot result;
  session.begin(old, QStringLiteral("local-file"), {}, {}, {}, {}, false);
  CHECK(session.accept(old, 7));
  CHECK(session.loaded(old, 7));
  CHECK(session.position(old, 7, 1000));
  CHECK(session.revise(old, next, true));
  CHECK(!session.snapshot(old, result));
  CHECK(!session.snapshot(next, result));
  CHECK(!session.position(old, 7, 2000));
  CHECK(session.position(next, 7, 2000));
  CHECK(session.snapshot(next, result));
  CHECK(result.paused && result.positionMs == 2000);
}

int main()
{
  keyedNativeEntryOnly();
  duplicateNativeEntryIsRejected();
  snapshotRequiresMatchingAcceptedLoadedEntry();
  newerIntentNeedsFreshPosition();
}
