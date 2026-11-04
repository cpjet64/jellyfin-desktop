#pragma once

#include <mpv/client.h>
#include <QDebug>
#include <vector>

// Separate mpv client: it observes broadcast events and never consumes the
// PlayerComponent client's events or registers/continues its hooks.
class C2EventObserver
{
public:
  struct Event {
    mpv_event_id kind;
    int64_t playlistId = 0;
    int reason = 0;
    int error = 0;
    int64_t insertId = 0;
    int insertCount = 0;
  };
  struct Flag { int error; bool value; };

  explicit C2EventObserver(mpv_handle* core)
    : client(mpv_create_client(core, "c2-observer")) {}
  ~C2EventObserver() { close(); }
  C2EventObserver(const C2EventObserver&) = delete;
  C2EventObserver& operator=(const C2EventObserver&) = delete;

  void close()
  {
    if (!client) return;
    qInfo() << "C2 closing secondary observer before core destruction";
    mpv_destroy(client);
    client = nullptr;
  }

  void drain()
  {
    while (client) {
      const mpv_event* event = mpv_wait_event(client, 0);
      if (!event || event->event_id == MPV_EVENT_NONE) break;
      Event copy{event->event_id};
      copy.error = event->error;
      if (event->event_id == MPV_EVENT_START_FILE && event->data) {
        copy.playlistId = static_cast<const mpv_event_start_file*>(event->data)->playlist_entry_id;
      } else if (event->event_id == MPV_EVENT_END_FILE && event->data) {
        const auto* end = static_cast<const mpv_event_end_file*>(event->data);
        copy.playlistId = end->playlist_entry_id;
        copy.reason = end->reason;
        copy.error = end->error;
        copy.insertId = end->playlist_insert_id;
        copy.insertCount = end->playlist_insert_num_entries;
      }
      events.push_back(copy);
    }
  }

  Flag flag(const char* name) const
  {
    int value = 0;
    const int result = mpv_get_property(client, name, MPV_FORMAT_FLAG, &value);
    return {result, result >= 0 && value != 0};
  }

  int count(mpv_event_id kind) const
  {
    int result = 0;
    for (const auto& event : events) if (event.kind == kind) ++result;
    return result;
  }

  int64_t latestLoadedEntry() const
  {
    int64_t pending = 0;
    int64_t loaded = 0;
    for (const auto& event : events) {
      if (event.kind == MPV_EVENT_START_FILE) pending = event.playlistId;
      else if (event.kind == MPV_EVENT_FILE_LOADED) loaded = event.error == 0 ? pending : 0;
    }
    return loaded;
  }

  int64_t latestStartedEntry() const
  {
    int64_t started = 0;
    for (const auto& event : events)
      if (event.kind == MPV_EVENT_START_FILE) started = event.playlistId;
    return started;
  }

  void mark(const char* stage)
  {
    drain();
    qInfo() << "C2 event boundary" << stage << "count" << events.size();
    for (const auto& event : events) {
      if (event.kind == MPV_EVENT_START_FILE || event.kind == MPV_EVENT_END_FILE)
        qInfo() << "C2 file event" << event.kind << event.playlistId << event.reason
                << event.error << event.insertId << event.insertCount;
    }
  }

  mpv_handle* client = nullptr;
  std::vector<Event> events;
};
