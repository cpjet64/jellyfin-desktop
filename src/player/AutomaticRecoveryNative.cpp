#include "AutomaticRecoveryNative.h"
#include <cstring>

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
