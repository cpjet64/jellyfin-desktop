#include "AutomaticRecoveryRuntime.h"
#include "AutomaticRecoveryNative.h"
#include <QByteArray>
#include <QVector>
#include <limits>

bool AutomaticRecoveryRuntime::submitUserLoad(mpv_handle* core, Key key, const QStringList& command,
    const QString& url, const QVariantMap& options, const QVariantMap& metadata,
    const QVariant& audio, const QVariant& subtitle, bool paused, std::int64_t nowMs)
{
  invalidate();
  if (!core || command.isEmpty() || nowMs < 0 || nowMs > std::numeric_limits<std::int64_t>::max() - 15000 ||
      sequence_ >= AutomaticRecoveryLoad::MaxSequence) return false;
  key_ = key;
  deadline_ = nowMs + 15000;
  if (!load_.begin({key.session, key.revision, key.generation}, ++sequence_, deadline_)) return false;
  session_.begin(key, url, options, metadata, audio, subtitle, paused);
  QVector<QByteArray> bytes;
  bytes.reserve(command.size());
  for (const auto& part : command) bytes.push_back(part.toUtf8());
  QVector<const char*> args;
  args.reserve(bytes.size() + 1);
  for (const auto& part : bytes) args.push_back(part.constData());
  args.push_back(nullptr);
  if (mpv_command_async(core, load_.userdata(), args.data()) < 0) { invalidate(); return false; }
  return true;
}
void AutomaticRecoveryRuntime::observe(const mpv_event* event, Key current, std::int64_t nowMs)
{
  if (!event) return;
  if (!installed_ && (nowMs < 0 || nowMs >= deadline_)) { invalidate(); return; }
  const auto state = load_.poll({current.session, current.revision, current.generation}, nowMs);
  if (state != AutomaticRecoveryLoad::State::Pending && state != AutomaticRecoveryLoad::State::Accepted) {
    invalidate(); return;
  }
  if (event->event_id == MPV_EVENT_COMMAND_REPLY && event->reply_userdata == load_.userdata()) {
    const auto entry = AutomaticRecoveryNative::loadEntry(event, load_.userdata());
    load_.reply(event->reply_userdata, entry > 0, entry, nowMs);
  } else if (event->event_id == MPV_EVENT_START_FILE) {
    if (startedEntry_) { invalidate(); return; }
    startedEntry_ = event->data ? static_cast<const mpv_event_start_file*>(event->data)->playlist_entry_id : 0;
    load_.started(startedEntry_, nowMs);
  } else if (event->event_id == MPV_EVENT_FILE_LOADED) {
    if (!startedEntry_ || loaded_) { invalidate(); return; }
    loaded_ = true;
  } else if (event->event_id == MPV_EVENT_END_FILE && startedEntry_) {
    invalidate(); return;
  } else if (event->event_id == MPV_EVENT_QUEUE_OVERFLOW ||
             event->event_id == MPV_EVENT_SHUTDOWN) {
    invalidate(); return;
  }
  settle(current, nowMs);
}
void AutomaticRecoveryRuntime::settle(Key current, std::int64_t nowMs)
{
  if (load_.poll({current.session, current.revision, current.generation}, nowMs) !=
      AutomaticRecoveryLoad::State::Accepted || !loaded_ || installed_) return;
  const auto entry = load_.entry();
  if (!entry || entry != startedEntry_ || !session_.accept(current, entry) || !session_.loaded(current, entry)) {
    invalidate(); return;
  }
  installed_ = true;
}
void AutomaticRecoveryRuntime::invalidate()
{
  load_.invalidate(); session_.invalidate(); key_ = {}; startedEntry_ = deadline_ = 0; loaded_ = installed_ = false;
}
