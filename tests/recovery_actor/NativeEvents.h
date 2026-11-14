#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mpv/client.h>
#include <string>
#include <vector>

// Owns synthetic native event payloads until the actor asks for the next one.
// The actor still consumes them through its normal queued wake and drain path.
class RecoveryActorNativeEvents {
public:
  struct PlaylistEntry { std::int64_t id; bool playing; std::string url; };

  void start(std::int64_t entry)
  {
    auto event = std::make_unique<Event>();
    event->start.playlist_entry_id = entry;
    event->value.event_id = MPV_EVENT_START_FILE;
    event->value.data = &event->start;
    pending_.push_back(std::move(event));
  }

  void end(std::int64_t entry, mpv_end_file_reason reason, int error = 0)
  {
    auto event = std::make_unique<Event>();
    event->end.playlist_entry_id = entry;
    event->end.reason = reason;
    event->end.error = error;
    event->value.event_id = MPV_EVENT_END_FILE;
    event->value.data = &event->end;
    pending_.push_back(std::move(event));
  }

  void commandReply(std::uint64_t userdata, int error = 0)
  {
    auto event = std::make_unique<Event>();
    event->command.result.format = MPV_FORMAT_NONE;
    event->value.event_id = MPV_EVENT_COMMAND_REPLY;
    event->value.reply_userdata = userdata;
    event->value.error = error;
    event->value.data = &event->command;
    pending_.push_back(std::move(event));
  }

  void commandReplyEntry(std::uint64_t userdata, std::int64_t entry)
  {
    auto event = std::make_unique<Event>();
    event->entryValue.format = MPV_FORMAT_INT64;
    event->entryValue.u.int64 = entry;
    event->entryMap.num = 1;
    event->entryMap.keys = event->entryKeys;
    event->entryMap.values = &event->entryValue;
    event->command.result.format = MPV_FORMAT_NODE_MAP;
    event->command.result.u.list = &event->entryMap;
    event->value.event_id = MPV_EVENT_COMMAND_REPLY;
    event->value.reply_userdata = userdata;
    event->value.data = &event->command;
    pending_.push_back(std::move(event));
  }

  void fileLoaded()
  {
    auto event = std::make_unique<Event>();
    event->value.event_id = MPV_EVENT_FILE_LOADED;
    pending_.push_back(std::move(event));
  }

  void preloadHook(const std::string& token)
  {
    auto event = std::make_unique<Event>();
    event->hookToken = token;
    event->hookArgs[0] = event->hookName;
    event->hookArgs[1] = event->hookNumber;
    event->hookArgs[2] = event->hookToken.data();
    event->message.num_args = 3;
    event->message.args = event->hookArgs;
    event->value.event_id = MPV_EVENT_CLIENT_MESSAGE;
    event->value.data = &event->message;
    pending_.push_back(std::move(event));
  }

#if MPV_CLIENT_API_VERSION >= MPV_MAKE_VERSION(1, 100)
  void preloadHook(std::uint64_t id)
  {
    auto event = std::make_unique<Event>();
    event->hook.name = event->preloadName;
    event->hook.id = id;
    event->value.event_id = MPV_EVENT_HOOK;
    event->value.data = &event->hook;
    pending_.push_back(std::move(event));
  }
#endif

  void playlistReply(std::uint64_t userdata, const std::vector<PlaylistEntry>& entries)
  {
    auto event = std::make_unique<Event>();
    event->entries.reserve(entries.size());
    event->nodes.reserve(entries.size());
    for (const auto& input : entries) {
      auto entry = std::make_unique<PlaylistNode>();
      entry->url = input.url;
      entry->fields[0].format = MPV_FORMAT_INT64;
      entry->fields[0].u.int64 = input.id;
      entry->fields[1].format = MPV_FORMAT_FLAG;
      entry->fields[1].u.flag = input.playing;
      entry->fields[2].format = MPV_FORMAT_STRING;
      entry->fields[2].u.string = entry->url.data();
      entry->map.num = 3;
      entry->map.keys = entry->keys;
      entry->map.values = entry->fields;
      mpv_node node{};
      node.format = MPV_FORMAT_NODE_MAP;
      node.u.list = &entry->map;
      event->nodes.push_back(node);
      event->entries.push_back(std::move(entry));
    }
    event->list.num = static_cast<int>(event->nodes.size());
    event->list.values = event->nodes.data();
    event->playlist.format = MPV_FORMAT_NODE_ARRAY;
    event->playlist.u.list = &event->list;
    event->property.name = event->propertyName;
    event->property.format = MPV_FORMAT_NODE;
    event->property.data = &event->playlist;
    event->value.event_id = MPV_EVENT_GET_PROPERTY_REPLY;
    event->value.reply_userdata = userdata;
    event->value.data = &event->property;
    pending_.push_back(std::move(event));
  }

  void playlistError(std::uint64_t userdata)
  {
    auto event = std::make_unique<Event>();
    event->value.event_id = MPV_EVENT_GET_PROPERTY_REPLY;
    event->value.reply_userdata = userdata;
    event->value.error = MPV_ERROR_PROPERTY_UNAVAILABLE;
    pending_.push_back(std::move(event));
  }

  mpv_event* next()
  {
    current_.reset();
    if (pending_.empty()) return &none_;
    current_ = std::move(pending_.front());
    pending_.pop_front();
    return &current_->value;
  }

private:
  struct PlaylistNode {
    char id[3] = "id", playing[8] = "playing", filename[9] = "filename";
    char* keys[3] = {id, playing, filename};
    mpv_node fields[3]{};
    mpv_node_list map{};
    std::string url;
  };
  struct Event {
    mpv_event value{};
    mpv_event_start_file start{};
    mpv_event_end_file end{};
    mpv_event_command command{};
    char entryKey[18] = "playlist_entry_id";
    char* entryKeys[1] = {entryKey};
    mpv_node entryValue{};
    mpv_node_list entryMap{};
    char hookName[9] = "hook_run", hookNumber[2] = "2";
    std::string hookToken;
    char* hookArgs[3]{};
    mpv_event_client_message message{};
#if MPV_CLIENT_API_VERSION >= MPV_MAKE_VERSION(1, 100)
    char preloadName[13] = "on_preloaded";
    mpv_event_hook hook{};
#endif
    char propertyName[9] = "playlist";
    mpv_event_property property{};
    mpv_node playlist{};
    mpv_node_list list{};
    std::vector<mpv_node> nodes;
    std::vector<std::unique_ptr<PlaylistNode>> entries;
  };
  std::deque<std::unique_ptr<Event>> pending_;
  std::unique_ptr<Event> current_;
  mpv_event none_{};
};
