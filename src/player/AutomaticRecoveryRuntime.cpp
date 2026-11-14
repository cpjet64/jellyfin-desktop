#include "AutomaticRecoveryRuntime.h"
#include "AutomaticRecoveryNative.h"
#include <QByteArray>
#include <QVector>
#include <limits>
#include <atomic>

bool AutomaticRecoveryRuntime::submitUserLoad(const CommandDispatch& dispatch, Key key, const QStringList& command,
    const QString& url, const QVariantMap& options, const QVariantMap& metadata,
    const QVariant& audio, const QVariant& subtitle, bool paused, std::int64_t nowMs)
{
  if (!dispatch || !eventEpoch_ || command.isEmpty() || nowMs < 0 || nowMs > std::numeric_limits<std::int64_t>::max() - 15000) return false;
  if (generation_ && generation_ != key.generation) invalidate();
  generation_ = key.generation;
  AutomaticRecoverySession::Snapshot recipe;
  recipe.url = url; recipe.options = options; recipe.metadata = metadata;
  recipe.audio = audio; recipe.subtitle = subtitle; recipe.paused = paused;
  const auto sequence = reserveSequence();
  if (!sequence) return false;
  if (!queue_.enqueue(sequence, key, recipe, nowMs + 15000)) return false;
  QVector<QByteArray> bytes;
  bytes.reserve(command.size());
  for (const auto& part : command) bytes.push_back(part.toUtf8());
  QVector<const char*> args;
  args.reserve(bytes.size() + 1);
  for (const auto& part : bytes) args.push_back(part.constData());
  args.push_back(nullptr);
  try {
    if (dispatch(AutomaticRecoveryLoad::Tag | sequence, args.data()) < 0) { queue_.reject(sequence); return false; }
  } catch (...) { queue_.reject(sequence); return false; }
  return true;
}
void AutomaticRecoveryRuntime::observe(const mpv_event* event, Key current, std::int64_t nowMs)
{
  if (!event) return;
  if (!current.generation || current.generation != generation_) { invalidate(); return; }
  if (event->event_id == MPV_EVENT_START_FILE || event->event_id == MPV_EVENT_END_FILE ||
      event->event_id == MPV_EVENT_SEEK || event->event_id == MPV_EVENT_PLAYBACK_RESTART ||
      event->event_id == MPV_EVENT_QUEUE_OVERFLOW || event->event_id == MPV_EVENT_SHUTDOWN)
    advanceEpoch();
  if (!eventEpoch_) { invalidate(); return; }
  if (!queue_.poll(nowMs)) { invalidate(); return; }
  if (event->event_id == MPV_EVENT_COMMAND_REPLY &&
      (event->reply_userdata & ~AutomaticRecoveryLoad::MaxSequence) == AutomaticRecoveryLoad::Tag) {
    const auto sequence = event->reply_userdata & AutomaticRecoveryLoad::MaxSequence;
    const auto entry = AutomaticRecoveryNative::loadEntry(event, event->reply_userdata);
    if (entry) queue_.reply(sequence, entry, nowMs); else queue_.reject(sequence);
  } else if (event->event_id == MPV_EVENT_START_FILE) {
    const auto* start = static_cast<const mpv_event_start_file*>(event->data);
    queue_.start(start ? start->playlist_entry_id : 0, nowMs);
  } else if (event->event_id == MPV_EVENT_FILE_LOADED) {
    queue_.loaded(nowMs);
  } else if (event->event_id == MPV_EVENT_END_FILE) {
    const auto* end = static_cast<const mpv_event_end_file*>(event->data);
    queue_.end(end ? end->playlist_entry_id : 0, nowMs);
  } else if (event->event_id == MPV_EVENT_QUEUE_OVERFLOW || event->event_id == MPV_EVENT_SHUTDOWN) {
    invalidate();
  }
}
void AutomaticRecoveryRuntime::invalidate()
{
  queue_.clear(); generation_ = 0; advanceEpoch();
}
void AutomaticRecoveryRuntime::userIntentChanged()
{
  revoked_ = queue_.activeKey();
  advanceEpoch();
}
void AutomaticRecoveryRuntime::internalEvent(const mpv_event* event)
{
  if (event && (event->event_id == MPV_EVENT_START_FILE || event->event_id == MPV_EVENT_END_FILE ||
      event->event_id == MPV_EVENT_SEEK || event->event_id == MPV_EVENT_PLAYBACK_RESTART ||
      event->event_id == MPV_EVENT_QUEUE_OVERFLOW || event->event_id == MPV_EVENT_SHUTDOWN)) advanceEpoch();
}
void AutomaticRecoveryRuntime::advanceEpoch()
{
  if (eventEpoch_ == std::numeric_limits<std::uint64_t>::max()) eventEpoch_ = 0;
  else if (eventEpoch_) ++eventEpoch_;
}
bool AutomaticRecoveryRuntime::recordPosition(Key key, std::int64_t entry, std::int64_t positionMs, std::int64_t nowMs)
{
  if (key.session == revoked_.session && key.revision == revoked_.revision &&
      key.generation == revoked_.generation) return false;
  return queue_.position(key, entry, positionMs, nowMs);
}
bool AutomaticRecoveryRuntime::snapshot(Key current, AutomaticRecoverySession::Snapshot& output) const
{
  if (current.session == revoked_.session && current.revision == revoked_.revision &&
      current.generation == revoked_.generation) return false;
  return queue_.snapshot(current, output);
}
bool AutomaticRecoveryRuntime::reviseActive(Key previous, Key current, bool paused)
{
  if (!queue_.reviseActive(previous, current, paused)) return false;
  return true;
}
bool AutomaticRecoveryRuntime::updateTracks(Key current, const QVariant& audio, const QVariant& subtitle)
{ return queue_.updateTracks(current, audio, subtitle); }
bool AutomaticRecoveryRuntime::replaceActiveEntry(Key current, std::int64_t previous, std::int64_t entry, std::int64_t positionMs)
{ return queue_.replaceActiveEntry(current, previous, entry, positionMs); }
bool AutomaticRecoveryRuntime::selections(std::int64_t entry, Key current, QVariant& audio, QVariant& subtitle) const
{ return queue_.selections(entry, current, audio, subtitle); }
std::uint64_t AutomaticRecoveryRuntime::reserveSequence()
{
  static std::atomic<std::uint64_t> next{1};
  auto value = next.load();
  while (value && value <= AutomaticRecoveryLoad::MaxSequence) {
    const auto following = value == AutomaticRecoveryLoad::MaxSequence ? 0 : value + 1;
    if (next.compare_exchange_weak(value, following)) return value;
  }
  return 0;
}
bool AutomaticRecoveryRuntime::takeTimeout(Key current, std::int64_t nowMs, std::uint64_t& sequence)
{
  if (!current.session || !current.revision || !current.generation || current.generation != generation_ ||
      !queue_.poll(nowMs)) return false;
  AutomaticRecoveryQueue::Expired expired;
  while (queue_.takeExpired(expired)) {
    if (expired.key.session != current.session || expired.key.revision != current.revision ||
        expired.key.generation != current.generation) continue;
    sequence = expired.sequence; return true;
  }
  return false;
}
