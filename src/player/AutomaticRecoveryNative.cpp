#include "AutomaticRecoveryNative.h"
#include <cstring>
#include <unordered_set>

std::int64_t AutomaticRecoveryNative::loadEntry(const mpv_event* event, std::uint64_t expectedUserdata)
{
  if (!event || event->event_id != MPV_EVENT_COMMAND_REPLY || event->reply_userdata != expectedUserdata ||
      event->error < 0 || !event->data) return 0;
  const auto& result = static_cast<const mpv_event_command*>(event->data)->result;
  if (result.format != MPV_FORMAT_NODE_MAP || !result.u.list || result.u.list->num < 0 ||
      result.u.list->num > 16 || !result.u.list->keys || !result.u.list->values) return 0;
  std::int64_t entry = 0;
  bool seen = false;
  for (int i = 0; i < result.u.list->num; ++i) {
    if (!result.u.list->keys[i] || std::strcmp(result.u.list->keys[i], "playlist_entry_id")) continue;
    if (seen || result.u.list->values[i].format != MPV_FORMAT_INT64) return 0;
    seen = true;
    entry = result.u.list->values[i].u.int64;
  }
  return entry > 0 ? entry : 0;
}

namespace {
bool playlistEntry(const mpv_node& node, std::int64_t& id, bool& playing)
{
  if (node.format != MPV_FORMAT_NODE_MAP || !node.u.list || node.u.list->num < 0 ||
      node.u.list->num > 64 || !node.u.list->keys || !node.u.list->values) return false;
  bool idSeen = false, playingSeen = false;
  for (int i = 0; i < node.u.list->num; ++i) {
    const char* key = node.u.list->keys[i];
    if (!key) return false;
    const auto& value = node.u.list->values[i];
    if (!std::strcmp(key, "id")) {
      if (idSeen || value.format != MPV_FORMAT_INT64 || value.u.int64 <= 0) return false;
      idSeen = true; id = value.u.int64;
    } else if (!std::strcmp(key, "playing")) {
      if (playingSeen || value.format != MPV_FORMAT_FLAG || (value.u.flag != 0 && value.u.flag != 1)) return false;
      playingSeen = true; playing = value.u.flag != 0;
    }
  }
  return idSeen;
}
}
std::int64_t AutomaticRecoveryNative::playingEntry(const mpv_node& playlist)
{
  if (playlist.format != MPV_FORMAT_NODE_ARRAY || !playlist.u.list || playlist.u.list->num < 0 ||
      playlist.u.list->num > 4096 || (playlist.u.list->num && !playlist.u.list->values)) return 0;
  std::int64_t selected = 0;
  std::unordered_set<std::int64_t> ids;
  for (int i = 0; i < playlist.u.list->num; ++i) {
    std::int64_t id = 0; bool playing = false;
    if (!playlistEntry(playlist.u.list->values[i], id, playing) || !ids.insert(id).second) return 0;
    if (playing) { if (selected) return 0; selected = id; }
  }
  return selected;
}
