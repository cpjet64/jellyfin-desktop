#include "OrdinaryPlaylist.h"
#include <cstring>

namespace {
struct Entry { std::int64_t id = 0; bool playing = false; QString filename; };
bool parse(const mpv_node& node, Entry& entry)
{
  if (node.format != MPV_FORMAT_NODE_MAP || !node.u.list || node.u.list->num < 0 || node.u.list->num > 64 ||
      (node.u.list->num && (!node.u.list->keys || !node.u.list->values))) return false;
  bool idSeen = false, playingSeen = false, filenameSeen = false;
  for (int i = 0; i < node.u.list->num; ++i) {
    const char* key = node.u.list->keys[i]; const auto& value = node.u.list->values[i];
    if (!key) return false;
    if (!std::strcmp(key, "id")) {
      if (idSeen || value.format != MPV_FORMAT_INT64 || value.u.int64 <= 0) return false;
      idSeen = true; entry.id = value.u.int64;
    } else if (!std::strcmp(key, "playing")) {
      if (playingSeen || value.format != MPV_FORMAT_FLAG || (value.u.flag != 0 && value.u.flag != 1)) return false;
      playingSeen = true; entry.playing = value.u.flag != 0;
    } else if (!std::strcmp(key, "filename")) {
      if (filenameSeen || value.format != MPV_FORMAT_STRING || !value.u.string) return false;
      filenameSeen = true; entry.filename = QString::fromUtf8(value.u.string);
    }
  }
  return idSeen;
}
bool validList(const mpv_node& node)
{ return node.format == MPV_FORMAT_NODE_ARRAY && node.u.list && node.u.list->num >= 0 && node.u.list->num <= 4096 &&
      (!node.u.list->num || node.u.list->values); }
}
bool OrdinaryPlaylist::entries(const mpv_node& playlist, QSet<std::int64_t>& ids)
{
  ids.clear();
  if (!validList(playlist)) return false;
  for (int i = 0; i < playlist.u.list->num; ++i) {
    Entry entry;
    if (!parse(playlist.u.list->values[i], entry) || ids.contains(entry.id)) { ids.clear(); return false; }
    ids.insert(entry.id);
  }
  return true;
}
bool OrdinaryPlaylist::currentMatches(const mpv_node& playlist, std::int64_t expected, const QString& encodedUrl)
{
  if (expected <= 0 || encodedUrl.isEmpty() || !validList(playlist)) return false;
  QSet<std::int64_t> seen; std::int64_t current = 0; QString filename;
  for (int i = 0; i < playlist.u.list->num; ++i) {
    Entry entry;
    if (!parse(playlist.u.list->values[i], entry) || seen.contains(entry.id)) return false;
    seen.insert(entry.id);
    if (entry.playing) { if (current) return false; current = entry.id; filename = entry.filename; }
  }
  return current == expected && filename == encodedUrl;
}
