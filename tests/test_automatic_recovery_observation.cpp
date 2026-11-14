#include "player/AutomaticRecoveryObservation.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #x); std::abort(); } } while (false)

using Observation = AutomaticRecoveryObservation;
static constexpr Observation::Key key{1, 2, 3};
struct Request { std::uint64_t id; std::string name; mpv_format format; };

struct Harness {
  Observation observation;
  std::vector<Request> requests;
  Observation::Dispatch dispatch = [this](std::uint64_t id, const char* name, mpv_format format) {
    requests.push_back({id, name, format}); return 0;
  };
  void respond(unsigned index, mpv_format format, void* data, int now, std::uint64_t epoch = 1, int error = 0)
  {
    CHECK(index < requests.size());
    const auto request = requests[index];
    mpv_event_property property{};
    property.name = request.name.c_str(); property.format = format; property.data = data;
    mpv_event event{}; event.event_id = MPV_EVENT_GET_PROPERTY_REPLY;
    event.reply_userdata = request.id; event.error = error; event.data = &property;
    observation.observe(&event, key, epoch, now, dispatch);
  }
};

struct Playlist {
  char idName[3] = "id";
  char playingName[8] = "playing";
  char* keys[2] = {idName, playingName};
  mpv_node fields[2]{};
  mpv_node_list map{};
  mpv_node entry{};
  mpv_node_list array{};
  mpv_node node{};
  explicit Playlist(std::int64_t id)
  {
    fields[0].format = MPV_FORMAT_INT64; fields[0].u.int64 = id;
    fields[1].format = MPV_FORMAT_FLAG; fields[1].u.flag = 1;
    map.num = 2; map.keys = keys; map.values = fields;
    entry.format = MPV_FORMAT_NODE_MAP; entry.u.list = &map;
    array.num = 1; array.values = &entry;
    node.format = MPV_FORMAT_NODE_ARRAY; node.u.list = &array;
  }
};

static void replyPayload(Harness& h, int& yes, int& no, double& position, std::int64_t& video)
{
  h.respond(1, MPV_FORMAT_FLAG, &yes, 10);
  h.respond(2, MPV_FORMAT_FLAG, &no, 11);
  h.respond(3, MPV_FORMAT_FLAG, &no, 12);
  h.respond(4, MPV_FORMAT_FLAG, &yes, 13);
  h.respond(5, MPV_FORMAT_DOUBLE, &position, 14);
  h.respond(6, MPV_FORMAT_INT64, &video, 15);
}

static void matchingBracketsAndTypedPayload()
{
  Harness h; Playlist list(7); int yes = 1, no = 0; double position = 12.5; std::int64_t video = 2;
  CHECK(h.observation.begin(key, 7, 1, 0, h.dispatch));
  h.respond(0, MPV_FORMAT_NODE, &list.node, 1);
  replyPayload(h, yes, no, position, video);
  h.respond(7, MPV_FORMAT_NODE, &list.node, 16);
  Observation::Result result;
  CHECK(h.observation.take(key, 1, 17, result));
  CHECK(result.entry == 7 && result.positionMs == 12500 && result.nativePaused);
  CHECK(!h.observation.take(key, 1, 18, result));
}

static void wrongEntryEpochOrTypeFailClosed()
{
  for (int kind = 0; kind != 4; ++kind) {
    Harness h; Playlist list(kind == 0 ? 8 : 7); int yes = 1;
    CHECK(h.observation.begin(key, 7, 1, 0, h.dispatch));
    h.respond(0, MPV_FORMAT_NODE, &list.node, 1, kind == 1 ? 2 : 1, kind == 2 ? -1 : 0);
    if (kind == 3) h.respond(1, MPV_FORMAT_INT64, &yes, 2);
    Observation::Result result;
    CHECK(!h.observation.take(key, 1, 3, result));
  }
}

static void staleOrDuplicateRepliesDoNotAuthorize()
{
  Harness h; Playlist list(7); int yes = 1;
  CHECK(h.observation.begin(key, 7, 1, 0, h.dispatch));
  h.respond(0, MPV_FORMAT_NODE, &list.node, 1);
  h.respond(1, MPV_FORMAT_FLAG, &yes, 2);
  h.respond(1, MPV_FORMAT_FLAG, &yes, 3);
  Observation::Result result;
  CHECK(!h.observation.take(key, 1, 4, result));

  Harness expired;
  CHECK(expired.observation.begin(key, 7, 1, 0, expired.dispatch));
  expired.respond(0, MPV_FORMAT_NODE, &list.node, 2000);
  CHECK(!expired.observation.take(key, 1, 2000, result));
}

static void laterPlaylistMustStillSelectSameUniqueEntry()
{
  for (int variant = 0; variant != 2; ++variant) {
    Harness h; Playlist first(7), after(variant == 0 ? 8 : 7);
    int yes = 1, no = 0; double position = 12.5; std::int64_t video = 2;
    CHECK(h.observation.begin(key, 7, 1, 0, h.dispatch));
    h.respond(0, MPV_FORMAT_NODE, &first.node, 1);
    replyPayload(h, yes, no, position, video);
    if (variant == 1) after.fields[1].u.flag = 0;
    h.respond(7, MPV_FORMAT_NODE, &after.node, 16);
    Observation::Result result;
    CHECK(!h.observation.take(key, 1, 17, result));
  }
}

static void ambiguousPlayingEntriesAreRejected()
{
  Harness h; Playlist one(7), two(8);
  mpv_node entries[] = {one.entry, two.entry};
  mpv_node_list list{}; list.num = 2; list.values = entries;
  mpv_node node{}; node.format = MPV_FORMAT_NODE_ARRAY; node.u.list = &list;
  CHECK(h.observation.begin(key, 7, 1, 0, h.dispatch));
  h.respond(0, MPV_FORMAT_NODE, &node, 1);
  Observation::Result result;
  CHECK(!h.observation.take(key, 1, 2, result));
}

int main()
{
  matchingBracketsAndTypedPayload();
  wrongEntryEpochOrTypeFailClosed();
  staleOrDuplicateRepliesDoNotAuthorize();
  laterPlaylistMustStillSelectSameUniqueEntry();
  ambiguousPlayingEntriesAreRejected();
}
