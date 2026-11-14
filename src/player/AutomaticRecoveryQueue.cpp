#include "AutomaticRecoveryQueue.h"
#include <utility>

bool AutomaticRecoveryQueue::enqueue(std::uint64_t sequence, Key key, const Snapshot& recipe,
                                     std::int64_t deadlineMs)
{
  if (!sequence || sequence <= lastSequence_ || pending_.size() >= 64 || deadlineMs <= 0 ||
      !key.session || !key.revision || !key.generation || recipe.url.isEmpty()) return false;
  Pending item; item.key = key; item.deadline = deadlineMs;
  item.session.begin(key, recipe.url, recipe.options, recipe.metadata, recipe.audio, recipe.subtitle, recipe.paused);
  pending_.emplace(sequence, std::move(item)); lastSequence_ = sequence; return true;
}
bool AutomaticRecoveryQueue::reply(std::uint64_t sequence, std::int64_t entry, std::int64_t nowMs)
{
  if (!observeTime(nowMs)) return false;
  const auto it = pending_.find(sequence);
  if (it == pending_.end()) return false;
  auto& item = it->second;
  if (item.expired) return false;
  if (item.entry || entry <= 0 || nowMs < 0 || nowMs >= item.deadline) { pending_.erase(it); return false; }
  for (const auto& other : pending_) {
    if (other.first != sequence && other.second.entry == entry) { pending_.erase(it); return false; }
  }
  if (!item.session.accept(item.key, entry)) { pending_.erase(it); return false; }
  item.entry = entry; promote(); return true;
}
void AutomaticRecoveryQueue::reject(std::uint64_t sequence)
{
  const auto it = pending_.find(sequence);
  if (it != pending_.end() && !it->second.expired) pending_.erase(it);
}
bool AutomaticRecoveryQueue::observeTime(std::int64_t nowMs)
{
  if (nowMs < 0 || nowMs < lastEventTime_) { clear(); return false; }
  lastEventTime_ = nowMs;
  for (auto& item : pending_)
    if (!item.second.entry && nowMs >= item.second.deadline) item.second.expired = true;
  return true;
}
void AutomaticRecoveryQueue::start(std::int64_t entry, std::int64_t nowMs)
{
  if (!observeTime(nowMs)) return;
  if (startedEntry_) {
    for (auto it = pending_.begin(); it != pending_.end();) {
      if (!it->second.entry || it->second.entry == startedEntry_) it = pending_.erase(it);
      else ++it;
    }
  }
  active_.invalidate(); startedEntry_ = entry > 0 ? entry : 0; loaded_ = false;
}
void AutomaticRecoveryQueue::loaded(std::int64_t nowMs)
{
  if (!observeTime(nowMs)) return;
  if (!startedEntry_ || loaded_) { clear(); return; }
  loaded_ = true; promote();
}
void AutomaticRecoveryQueue::promote()
{
  if (!startedEntry_ || !loaded_) return;
  for (auto it = pending_.begin(); it != pending_.end(); ++it) {
    auto& item = it->second;
    if (item.entry != startedEntry_) continue;
    if (item.session.loaded(item.key, startedEntry_)) active_ = std::move(item.session);
    pending_.erase(it); startedEntry_ = 0; loaded_ = false; return;
  }
}
void AutomaticRecoveryQueue::end(std::int64_t entry, std::int64_t nowMs)
{
  if (!observeTime(nowMs)) return;
  const bool endedUnmatchedStart = startedEntry_ && (entry <= 0 || entry == startedEntry_);
  if (entry <= 0 || entry == active_.entry()) active_.invalidate();
  if (entry <= 0 || entry == startedEntry_) { startedEntry_ = 0; loaded_ = false; }
  for (auto it = pending_.begin(); it != pending_.end();) {
    if (entry <= 0 || it->second.entry == entry || (endedUnmatchedStart && !it->second.entry))
      it = pending_.erase(it);
    else ++it;
  }
}
bool AutomaticRecoveryQueue::position(Key key, std::int64_t entry, std::int64_t positionMs, std::int64_t nowMs)
{ return observeTime(nowMs) && active_.position(key, entry, positionMs); }
bool AutomaticRecoveryQueue::snapshot(Key current, Snapshot& output) const
{ return active_.snapshot(current, output); }
bool AutomaticRecoveryQueue::reviseActive(Key previous, Key current, bool paused)
{ return active_.revise(previous, current, paused); }
bool AutomaticRecoveryQueue::updateTracks(Key current, const QVariant& audio, const QVariant& subtitle)
{ return active_.tracks(current, audio, subtitle); }
bool AutomaticRecoveryQueue::replaceActiveEntry(Key current, std::int64_t previous, std::int64_t entry, std::int64_t positionMs)
{ return active_.replaceEntry(current, previous, entry, positionMs); }
bool AutomaticRecoveryQueue::poll(std::int64_t nowMs) { return observeTime(nowMs); }
bool AutomaticRecoveryQueue::takeExpired(Expired& output)
{
  for (auto it = pending_.begin(); it != pending_.end(); ++it) {
    if (!it->second.expired) continue;
    output = {it->first, it->second.key}; pending_.erase(it); return true;
  }
  return false;
}
void AutomaticRecoveryQueue::clear()
{
  pending_.clear(); active_.invalidate(); startedEntry_ = 0; loaded_ = false;
}
