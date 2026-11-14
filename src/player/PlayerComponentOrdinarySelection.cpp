#include "PlayerComponentRecovery.h"
#ifdef MPVQT_RECOVERY_RUNTIME
#include "OrdinaryPlaylist.h"
#include "AutomaticRecoveryNative.h"
#include "PlayerComponent.h"
#include "RecoveryIdentity.h"
#include "RendererOwners.h"
#include <cstring>
#include <limits>

namespace {
std::uint64_t nextPlaylistQuery()
{
  const auto value = RecoveryIdentity::take(RecoveryIdentity::Domain::PlaylistQuery);
  return value ? (std::uint64_t{1} << 59) | value : 0;
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
bool setPauseOption(QStringList& command, bool paused)
{
  if (command.isEmpty()) return false;
  QString& options = command.last();
  const auto index = options.indexOf(",pause=");
  if (index < 0) return false;
  const auto value = index + 7;
  const auto end = options.indexOf(',', value);
  const auto length = (end < 0 ? options.size() : end) - value;
  const auto prior = options.mid(value, length);
  if (prior != "yes" && prior != "no") return false;
  options.replace(value, length, paused ? "yes" : "no"); return true;
}
}

int PlayerComponentRecovery::ordinaryGet(std::uint64_t id, const char* name, mpv_format format)
{
#ifdef PLAYER_RECOVERY_ACTOR_TEST
  if (testEnabled_) return ordinaryGetDispatch_ ? ordinaryGetDispatch_(id, name, format) : MPV_ERROR_UNINITIALIZED;
#endif
  if (!core_) return MPV_ERROR_UNINITIALIZED;
  auto lease = core_->acquireNative();
  return lease.get() ? mpv_get_property_async(lease.get(), id, name, format) : MPV_ERROR_UNINITIALIZED;
}
int PlayerComponentRecovery::ordinaryCommand(std::uint64_t id, const char** args)
{
#ifdef PLAYER_RECOVERY_ACTOR_TEST
  if (testEnabled_) return ordinaryCommandDispatch_ ? ordinaryCommandDispatch_(id, args) : MPV_ERROR_UNINITIALIZED;
#endif
  if (!core_) return MPV_ERROR_UNINITIALIZED;
  auto lease = core_->acquireNative();
  return lease.get() ? mpv_command_async(lease.get(), id, args) : MPV_ERROR_UNINITIALIZED;
}

bool PlayerComponentRecovery::beginOrdinaryLoad(const QStringList& command, const QString& url, const QVariantMap& options,
    const QVariantMap& metadata, const QVariant& audio, const QVariant& subtitle, AutomaticRecoverySession::Key key)
{
  if ((!core_ && !player_->recoveryTestMode()) || ordinaryLoad_.pending) return false;
  ordinarySelection_.revoke(); ordinaryAfterId_ = 0;
  const auto intent = player_->m_playbackIntent.snapshot();
  ordinaryPreferences_ = {intent.epoch, intent.request, key.generation, key.revision, audio, subtitle,
                          intent.desired == PlaybackIntent::Desired::Paused, true};
  ordinaryLoad_.key = key; ordinaryLoad_.command = command; ordinaryLoad_.url = url;
  ordinaryLoad_.options = options; ordinaryLoad_.metadata = metadata; ordinaryLoad_.audio = audio; ordinaryLoad_.subtitle = subtitle;
  ordinaryLoad_.options.insert("autoplay", !ordinaryPreferences_.paused);
  ordinaryLoad_.beforeStart = startSerial_; ordinaryLoad_.pending = true;
  const auto now = clockNow();
  if (now < 0 || now > std::numeric_limits<std::int64_t>::max() - 1500) return flushOrdinaryLoad(false);
  ordinaryLoad_.deadlineMs = now + 1500; ordinaryLoad_.queryId = nextPlaylistQuery();
  if (!ordinaryLoad_.queryId || ordinaryGet(ordinaryLoad_.queryId, "playlist", MPV_FORMAT_NODE) < 0)
    return flushOrdinaryLoad(false);
  return true;
}

bool PlayerComponentRecovery::preserveOrdinaryPreference(PlaybackIntent::ChangeKind kind, const PlaybackIntent::Snapshot& intent)
{
  const bool allowed = kind == PlaybackIntent::ChangeKind::Pause || kind == PlaybackIntent::ChangeKind::Resume ||
                       kind == PlaybackIntent::ChangeKind::Audio || kind == PlaybackIntent::ChangeKind::Subtitle;
  if (!allowed || !ordinaryPreferences_.active ||
      !intent.epoch || !intent.request || intent.epoch != ordinaryPreferences_.epoch ||
      intent.request != ordinaryPreferences_.request || player_->m_mpvGeneration != ordinaryPreferences_.binding ||
      intent.revision <= ordinaryPreferences_.revision ||
      (intent.desired != PlaybackIntent::Desired::Playing && intent.desired != PlaybackIntent::Desired::Paused)) return false;
  const auto previous = ordinaryPreferences_.revision;
  if (ordinarySelection_.active() && !ordinarySelection_.revise(previous, intent.revision,
      player_->m_currentAudioStream, player_->m_currentSubtitleStream)) return false;
  ordinaryPreferences_.revision = intent.revision;
  ordinaryPreferences_.paused = intent.desired == PlaybackIntent::Desired::Paused;
  ordinaryPreferences_.audio = player_->m_currentAudioStream; ordinaryPreferences_.subtitle = player_->m_currentSubtitleStream;
  if (ordinaryLoad_.pending) {
    ordinaryLoad_.key.revision = intent.revision; ordinaryLoad_.options.insert("autoplay", !ordinaryPreferences_.paused);
    ordinaryLoad_.audio = ordinaryPreferences_.audio; ordinaryLoad_.subtitle = ordinaryPreferences_.subtitle;
  }
  if (ordinaryAfterId_ && ordinaryAfterRevision_ == previous) ordinaryAfterRevision_ = intent.revision;
  if (deferredEntry_ && deferredRevision_ == previous) deferredRevision_ = intent.revision;
  return true;
}

bool PlayerComponentRecovery::applyOrdinaryPause()
{
  if (!ordinaryPreferenceCurrent()) return false;
  const int result = player_->applySelectedPause(ordinaryPreferences_.paused);
  ordinaryPreferences_.active = false;
  return result >= 0;
}
bool PlayerComponentRecovery::ordinaryPreferenceCurrent() const
{
  const auto intent = player_->m_playbackIntent.snapshot();
  return ordinaryPreferences_.active && intent.epoch == ordinaryPreferences_.epoch &&
      intent.request == ordinaryPreferences_.request && intent.revision == ordinaryPreferences_.revision &&
      player_->m_mpvGeneration == ordinaryPreferences_.binding;
}

bool PlayerComponentRecovery::flushOrdinaryLoad(bool allowSelection)
{
  if (!ordinaryLoad_.pending || (!core_ && !player_->recoveryTestMode())) return false;
  auto load = ordinaryLoad_; ordinaryLoad_ = {};
  if (!setPauseOption(load.command, !load.options.value("autoplay").toBool())) return false;
  const auto current = player_->m_playbackIntent.snapshot();
  const bool qualified = allowSelection && !startSerialExhausted_ && ordinaryLedger_.available() &&
      load.beforeStart == startSerial_ && current.revision == load.key.revision && player_->m_mpvGeneration == load.key.generation;
  ordinaryExpectedUrl_ = load.command.value(1);
  const bool accepted = runtime_.submitUserLoad([&](std::uint64_t id, const char** args) {
    const int result = ordinaryCommand(id, args);
    if (result >= 0) {
      const bool tracked = ordinaryLedger_.admitted(id);
      if (qualified && tracked) ordinarySelection_.begin(id, load.key.revision, load.key.generation, load.beforeStart,
                                              ordinaryBeforeEntries_, load.audio, load.subtitle);
    }
    return result;
  }, load.key, load.command, load.url, load.options, load.metadata, load.audio, load.subtitle,
      !load.options.value("autoplay").toBool(), clockNow());
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
  if (event->event_id == MPV_EVENT_COMMAND_REPLY) {
    ordinaryLedger_.reply(event->reply_userdata, event->error >= 0);
    ordinarySelection_.reply(event->reply_userdata, entrylessSuccess(event), AutomaticRecoveryNative::loadEntry(event, event->reply_userdata));
  }
  if (event->event_id == MPV_EVENT_END_FILE) {
    const auto* end = static_cast<const mpv_event_end_file*>(event->data);
    ordinarySelection_.ended(end ? end->playlist_entry_id : 0);
  } else if (event->event_id == MPV_EVENT_FILE_LOADED) {
    ordinarySelection_.loaded(lastStartedEntry_, startSerial_);
  } else if (event->event_id == MPV_EVENT_QUEUE_OVERFLOW || event->event_id == MPV_EVENT_SHUTDOWN) {
    ordinaryReplacement_.revoke(); ordinarySelection_.revoke(); ordinaryLedger_.failClosed(); ordinaryAfterId_ = 0;
  } else if (event->event_id == MPV_EVENT_START_FILE) {
    if (!ordinaryLedger_.started()) ordinarySelection_.revoke();
    ordinarySelection_.started(lastStartedEntry_, startSerial_);
    if (!ordinarySelection_.active() || (!core_ && !player_->recoveryTestMode())) return;
    ordinaryAfterId_ = nextPlaylistQuery(); ordinaryAfterEntry_ = lastStartedEntry_; ordinaryAfterStart_ = startSerial_;
    ordinaryAfterRevision_ = player_->m_playbackIntent.snapshot().revision; ordinaryAfterBinding_ = player_->m_mpvGeneration;
    if (!ordinaryAfterId_ || ordinaryGet(ordinaryAfterId_, "playlist", MPV_FORMAT_NODE) < 0) {
      ordinaryAfterId_ = 0; ordinarySelection_.revoke();
    }
  }
}
#endif
