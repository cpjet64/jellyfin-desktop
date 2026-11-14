#include "AutomaticRecoveryRuntime.h"
#include "AutomaticRecoveryNative.h"
#include <QByteArray>
#include <QVector>
#include <limits>
#include <utility>

bool AutomaticRecoveryRuntime::submitUserLoad(const CommandDispatch& dispatch, Key key, const QStringList& command,
    const QString& url, const QVariantMap& options, const QVariantMap& metadata,
    const QVariant& audio, const QVariant& subtitle, bool paused, std::int64_t nowMs)
{
  discardQueued();
  if (!dispatch || command.isEmpty() || nowMs < 0 || nowMs > std::numeric_limits<std::int64_t>::max() - 15000 ||
      sequence_ >= AutomaticRecoveryLoad::MaxSequence) return false;
  key_ = key;
  deadline_ = nowMs + 15000;
  if (!load_.begin({key.session, key.revision, key.generation}, ++sequence_, deadline_)) return false;
  queuedSession_.begin(key, url, options, metadata, audio, subtitle, paused);
  QVector<QByteArray> bytes;
  bytes.reserve(command.size());
  for (const auto& part : command) bytes.push_back(part.toUtf8());
  QVector<const char*> args;
  args.reserve(bytes.size() + 1);
  for (const auto& part : bytes) args.push_back(part.constData());
  args.push_back(nullptr);
  try {
    if (dispatch(load_.userdata(), args.data()) < 0) { discardQueued(); return false; }
  } catch (...) { discardQueued(); return false; }
  return true;
}
void AutomaticRecoveryRuntime::observe(const mpv_event* event, Key current, std::int64_t nowMs)
{
  if (!event) return;
  if (event->event_id == MPV_EVENT_START_FILE) activeSession_.invalidate();
  if (event->event_id == MPV_EVENT_END_FILE) {
    const auto* end = static_cast<const mpv_event_end_file*>(event->data);
    if (!end || end->playlist_entry_id == activeSession_.entry()) activeSession_.invalidate();
  }
  if (event->event_id == MPV_EVENT_QUEUE_OVERFLOW || event->event_id == MPV_EVENT_SHUTDOWN)
    activeSession_.invalidate();
  if (!installed_ && (nowMs < 0 || nowMs >= deadline_)) { discardQueued(); return; }
  if (current.session != key_.session || current.revision != key_.revision || current.generation != key_.generation) {
    discardQueued(); return;
  }
  const auto state = load_.poll({current.session, current.revision, current.generation}, nowMs);
  if (state != AutomaticRecoveryLoad::State::Pending && state != AutomaticRecoveryLoad::State::Accepted) {
    if (installed_) invalidate();
    else discardQueued();
    return;
  }
  if (event->event_id == MPV_EVENT_COMMAND_REPLY && event->reply_userdata == load_.userdata()) {
    const auto entry = AutomaticRecoveryNative::loadEntry(event, load_.userdata());
    load_.reply(event->reply_userdata, entry > 0, entry, nowMs);
  } else if (event->event_id == MPV_EVENT_START_FILE) {
    if (startedEntry_) { invalidate(); return; }
    startedEntry_ = event->data ? static_cast<const mpv_event_start_file*>(event->data)->playlist_entry_id : 0;
    load_.started(startedEntry_, nowMs);
  } else if (event->event_id == MPV_EVENT_FILE_LOADED) {
    if (!startedEntry_ || loaded_) { discardQueued(); return; }
    loaded_ = true;
  } else if (event->event_id == MPV_EVENT_END_FILE) {
    if (startedEntry_) { discardQueued(); return; }
  } else if (event->event_id == MPV_EVENT_QUEUE_OVERFLOW ||
             event->event_id == MPV_EVENT_SHUTDOWN) {
    invalidate(); return;
  }
  settle(current, nowMs);
}
void AutomaticRecoveryRuntime::settle(Key current, std::int64_t nowMs)
{
  if (load_.poll({current.session, current.revision, current.generation}, nowMs) !=
      AutomaticRecoveryLoad::State::Accepted || installed_) return;
  const auto entry = load_.entry();
  if (!matched_) {
    if (!entry || entry != startedEntry_ || !queuedSession_.accept(current, entry)) { discardQueued(); return; }
    matched_ = true;
  }
  if (!loaded_) return;
  if (!queuedSession_.loaded(current, entry)) { discardQueued(); return; }
  activeSession_ = std::move(queuedSession_);
  installed_ = true;
}
void AutomaticRecoveryRuntime::discardQueued()
{
  load_.invalidate(); queuedSession_.invalidate(); key_ = {}; startedEntry_ = deadline_ = 0;
  loaded_ = matched_ = installed_ = false;
}
void AutomaticRecoveryRuntime::invalidate()
{
  discardQueued(); activeSession_.invalidate();
}
