#include "PlayerComponentRecovery.h"
#ifdef MPVQT_RECOVERY_RUNTIME
#include "OrdinaryPlaylist.h"
#include "PlayerComponent.h"
#include "RendererOwners.h"
#include <atomic>
#include <cstring>
#include <limits>

namespace {
std::uint64_t nextPlaylistQuery()
{
  static std::atomic<std::uint64_t> next{1};
  constexpr std::uint64_t limit = (std::uint64_t{1} << 59) - 1;
  auto value = next.load();
  while (value && value <= limit) {
    if (next.compare_exchange_weak(value, value == limit ? 0 : value + 1)) return (std::uint64_t{1} << 59) | value;
  }
  return 0;
}
const mpv_node* playlistReply(const mpv_event* event, std::uint64_t id)
{
  if (!event || event->event_id != MPV_EVENT_GET_PROPERTY_REPLY || event->reply_userdata != id ||
      event->error < 0 || !event->data) return nullptr;
  const auto* property = static_cast<const mpv_event_property*>(event->data);
  if (!property->name || std::strcmp(property->name, "playlist") || property->format != MPV_FORMAT_NODE || !property->data)
    return nullptr;
  return static_cast<const mpv_node*>(property->data);
}
bool entrylessSuccess(const mpv_event* event)
{
  return event && event->event_id == MPV_EVENT_COMMAND_REPLY && event->error >= 0 && event->data &&
      static_cast<const mpv_event_command*>(event->data)->result.format == MPV_FORMAT_NONE;
}
}

bool PlayerComponentRecovery::beginOrdinaryLoad(const QStringList& command, const QString& url, const QVariantMap& options,
    const QVariantMap& metadata, const QVariant& audio, const QVariant& subtitle, AutomaticRecoverySession::Key key)
{
  if (!core_ || ordinaryLoad_.pending) return false;
  ordinarySelection_.revoke(); ordinaryAfterId_ = 0;
  ordinaryLoad_.key = key; ordinaryLoad_.command = command; ordinaryLoad_.url = url;
  ordinaryLoad_.options = options; ordinaryLoad_.metadata = metadata; ordinaryLoad_.audio = audio; ordinaryLoad_.subtitle = subtitle;
  ordinaryLoad_.beforeStart = startSerial_; ordinaryLoad_.pending = true;
  const auto now = nowMs();
  if (now < 0 || now > std::numeric_limits<std::int64_t>::max() - 1500) return flushOrdinaryLoad(false);
  ordinaryLoad_.deadlineMs = now + 1500; ordinaryLoad_.queryId = nextPlaylistQuery();
  auto lease = core_->acquireNative();
  if (!ordinaryLoad_.queryId || !lease.get() ||
      mpv_get_property_async(lease.get(), ordinaryLoad_.queryId, "playlist", MPV_FORMAT_NODE) < 0)
    return flushOrdinaryLoad(false);
  return true;
}

bool PlayerComponentRecovery::flushOrdinaryLoad(bool allowSelection)
{
  if (!ordinaryLoad_.pending || !core_) return false;
  const auto load = ordinaryLoad_; ordinaryLoad_ = {};
  auto lease = core_->acquireNative();
  if (!lease.get()) {
    player_->m_playbackIntent.dispatch(load.key.revision, PlaybackIntent::Dispatch::Unavailable); return false;
  }
  const auto current = player_->m_playbackIntent.snapshot();
  const bool qualified = allowSelection && ordinaryCounterValid_ && !ordinaryUnresolvedStarts_ &&
      load.beforeStart == startSerial_ && current.revision == load.key.revision && player_->m_mpvGeneration == load.key.generation;
  ordinaryExpectedUrl_ = load.command.value(1);
  const bool accepted = runtime_.submitUserLoad([&](std::uint64_t id, const char** args) {
    const int result = mpv_command_async(lease.get(), id, args);
    if (result >= 0) {
      if (ordinaryUnresolvedStarts_ == std::numeric_limits<std::uint64_t>::max()) ordinaryCounterValid_ = false;
      else ++ordinaryUnresolvedStarts_;
      if (qualified) ordinarySelection_.begin(id, load.key.revision, load.key.generation, load.beforeStart,
                                              ordinaryBeforeEntries_, load.audio, load.subtitle);
    }
    return result;
  }, load.key, load.command, load.url, load.options, load.metadata, load.audio, load.subtitle,
      !load.options.value("autoplay").toBool(), nowMs());
  if (!accepted) player_->m_playbackIntent.dispatch(load.key.revision, PlaybackIntent::Dispatch::NativeRejected);
  ordinaryBeforeEntries_.clear();
  return accepted;
}

void PlayerComponentRecovery::ordinaryEvent(const mpv_event* event, std::int64_t now)
{
  if (!event) return;
  if (ordinaryLoad_.pending && event->event_id == MPV_EVENT_GET_PROPERTY_REPLY &&
      event->reply_userdata == ordinaryLoad_.queryId) {
    const auto* playlist = playlistReply(event, ordinaryLoad_.queryId);
    const bool valid = playlist && now >= 0 && now < ordinaryLoad_.deadlineMs &&
        ordinaryLoad_.beforeStart == startSerial_ && OrdinaryPlaylist::entries(*playlist, ordinaryBeforeEntries_);
    flushOrdinaryLoad(valid); return;
  }
  if (ordinaryAfterId_ && event->event_id == MPV_EVENT_GET_PROPERTY_REPLY && event->reply_userdata == ordinaryAfterId_) {
    const auto* playlist = playlistReply(event, ordinaryAfterId_);
    const bool same = ordinaryAfterStart_ == startSerial_ && ordinaryAfterEntry_ == lastStartedEntry_ &&
        ordinaryAfterBinding_ == player_->m_mpvGeneration && ordinaryAfterRevision_ == player_->m_playbackIntent.snapshot().revision;
    ordinarySelection_.after(ordinaryAfterEntry_, ordinaryAfterStart_, same && playlist &&
                             OrdinaryPlaylist::currentMatches(*playlist, ordinaryAfterEntry_, ordinaryExpectedUrl_));
    ordinaryAfterId_ = 0; return;
  }
  if (event->event_id == MPV_EVENT_COMMAND_REPLY) ordinarySelection_.reply(event->reply_userdata, entrylessSuccess(event));
  if (event->event_id == MPV_EVENT_END_FILE) {
    const auto* end = static_cast<const mpv_event_end_file*>(event->data);
    ordinarySelection_.ended(end ? end->playlist_entry_id : 0);
  } else if (event->event_id == MPV_EVENT_FILE_LOADED) {
    ordinarySelection_.loaded(lastStartedEntry_, startSerial_);
  } else if (event->event_id == MPV_EVENT_QUEUE_OVERFLOW || event->event_id == MPV_EVENT_SHUTDOWN) {
    ordinarySelection_.revoke(); ordinaryCounterValid_ = false; ordinaryAfterId_ = 0;
  } else if (event->event_id == MPV_EVENT_START_FILE) {
    if (ordinaryUnresolvedStarts_) --ordinaryUnresolvedStarts_;
    else ordinarySelection_.revoke();
    ordinarySelection_.started(lastStartedEntry_, startSerial_);
    if (!ordinarySelection_.active() || !core_) return;
    ordinaryAfterId_ = nextPlaylistQuery(); ordinaryAfterEntry_ = lastStartedEntry_; ordinaryAfterStart_ = startSerial_;
    ordinaryAfterRevision_ = player_->m_playbackIntent.snapshot().revision; ordinaryAfterBinding_ = player_->m_mpvGeneration;
    auto lease = core_->acquireNative();
    if (!ordinaryAfterId_ || !lease.get() || mpv_get_property_async(lease.get(), ordinaryAfterId_, "playlist", MPV_FORMAT_NODE) < 0) {
      ordinaryAfterId_ = 0; ordinarySelection_.revoke();
    }
  }
}
#endif
