#include "player/AutomaticRecoveryRuntime.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #x); std::abort(); } } while (false)

using Runtime = AutomaticRecoveryRuntime;
using Key = Runtime::Key;

static std::uint64_t submit(Runtime& runtime, Key key, int time)
{
  std::uint64_t userdata = 0;
  const auto send = [&](std::uint64_t id, const char**) { userdata = id; return 0; };
  CHECK(runtime.submitUserLoad(send, key, {QStringLiteral("loadfile")},
                               QStringLiteral("synthetic-local"), {}, {}, 2, 3, false, time));
  CHECK(userdata > 0xffffffffULL);
  return userdata;
}

static void reply(Runtime& runtime, Key key, std::uint64_t userdata, std::int64_t entry, int time)
{
  char name[] = "playlist_entry_id";
  char* names[] = {name};
  mpv_node value{}; value.format = MPV_FORMAT_INT64; value.u.int64 = entry;
  mpv_node_list list{}; list.num = 1; list.keys = names; list.values = &value;
  mpv_event_command command{}; command.result.format = MPV_FORMAT_NODE_MAP; command.result.u.list = &list;
  mpv_event event{}; event.event_id = MPV_EVENT_COMMAND_REPLY; event.reply_userdata = userdata; event.data = &command;
  runtime.observe(&event, key, time);
}

static void start(Runtime& runtime, Key key, std::int64_t entry, int time)
{
  mpv_event_start_file payload{}; payload.playlist_entry_id = entry;
  mpv_event event{}; event.event_id = MPV_EVENT_START_FILE; event.data = &payload;
  runtime.observe(&event, key, time);
}

static void event(Runtime& runtime, Key key, mpv_event_id type, int time)
{
  mpv_event value{}; value.event_id = type;
  runtime.observe(&value, key, time);
}

static void pendingQueuePreservesCurrentAndPromotesMatchingEntry()
{
  Runtime runtime;
  const Key first{1, 1, 1}, second{2, 1, 1};
  auto old = submit(runtime, first, 0);
  reply(runtime, first, old, 7, 1);
  start(runtime, first, 7, 2);
  event(runtime, first, MPV_EVENT_FILE_LOADED, 3);
  CHECK(runtime.recordPosition(first, 7, 1000, 4));
  AutomaticRecoverySession::Snapshot snapshot;
  CHECK(runtime.snapshot(first, snapshot));
  auto queued = submit(runtime, second, 5);
  CHECK(runtime.snapshot(first, snapshot) && snapshot.entry == 7);
  start(runtime, second, 8, 6);
  CHECK(!runtime.snapshot(first, snapshot));
  event(runtime, second, MPV_EVENT_FILE_LOADED, 7);
  CHECK(!runtime.snapshot(second, snapshot));
  reply(runtime, second, queued, 8, 8);
  CHECK(runtime.recordPosition(second, 8, 2000, 9));
  CHECK(runtime.snapshot(second, snapshot) && snapshot.entry == 8);
}

static void duplicateLoadAndEpochChangesRevokeOldAuthority()
{
  Runtime runtime;
  const Key key{1, 1, 1};
  auto id = submit(runtime, key, 0);
  start(runtime, key, 7, 1);
  reply(runtime, key, id, 7, 2);
  event(runtime, key, MPV_EVENT_FILE_LOADED, 3);
  CHECK(runtime.recordPosition(key, 7, 100, 4));
  AutomaticRecoverySession::Snapshot snapshot;
  CHECK(runtime.snapshot(key, snapshot));
  const auto oldEpoch = runtime.eventEpoch();
  event(runtime, key, MPV_EVENT_SEEK, 5);
  CHECK(runtime.eventEpoch() != oldEpoch);
  runtime.userIntentChanged();
  CHECK(!runtime.snapshot({1, 2, 1}, snapshot));
  // Pending production fix: retaining a recipe must not retain old authority.
  CHECK(!runtime.snapshot(key, snapshot));
  event(runtime, key, MPV_EVENT_FILE_LOADED, 6);
  CHECK(!runtime.snapshot(key, snapshot));
}

static void malformedReplyAndEndDoNotAuthorize()
{
  Runtime runtime;
  const Key key{1, 1, 1};
  auto id = submit(runtime, key, 0);
  mpv_event bad{}; bad.event_id = MPV_EVENT_COMMAND_REPLY; bad.reply_userdata = id;
  runtime.observe(&bad, key, 1);
  start(runtime, key, 7, 2);
  event(runtime, key, MPV_EVENT_FILE_LOADED, 3);
  AutomaticRecoverySession::Snapshot snapshot;
  CHECK(!runtime.snapshot(key, snapshot));
  event(runtime, key, MPV_EVENT_END_FILE, 4);
  CHECK(!runtime.snapshot(key, snapshot));
}

static void entrylessOrdinarySuccessDoesNotAuthorizeRecovery()
{
  Runtime runtime;
  const Key key{1, 1, 1};
  const auto id = submit(runtime, key, 0);
  mpv_event_command result{};
  result.result.format = MPV_FORMAT_NONE;
  mpv_event replyEvent{};
  replyEvent.event_id = MPV_EVENT_COMMAND_REPLY; replyEvent.reply_userdata = id; replyEvent.data = &result;
  runtime.observe(&replyEvent, key, 1);
  start(runtime, key, 7, 2);
  event(runtime, key, MPV_EVENT_FILE_LOADED, 3);
  AutomaticRecoverySession::Snapshot snapshot;
  CHECK(!runtime.recordPosition(key, 7, 1000, 4));
  CHECK(!runtime.snapshot(key, snapshot));
}

int main()
{
  pendingQueuePreservesCurrentAndPromotesMatchingEntry();
  duplicateLoadAndEpochChangesRevokeOldAuthority();
  malformedReplyAndEndDoNotAuthorize();
  entrylessOrdinarySuccessDoesNotAuthorizeRecovery();
}
