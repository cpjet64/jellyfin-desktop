#include "AutomaticRecoverySession.h"

bool AutomaticRecoverySession::same(Key a, Key b)
{
  return a.session && a.revision && a.generation && a.session == b.session &&
      a.revision == b.revision && a.generation == b.generation;
}
void AutomaticRecoverySession::begin(Key key, const QString& url, const QVariantMap& options,
                                     const QVariantMap& metadata, const QVariant& audio,
                                     const QVariant& subtitle, bool paused)
{
  invalidate();
  if (!same(key, key) || url.isEmpty()) return;
  candidate_.key = key; candidate_.url = url; candidate_.options = options; candidate_.metadata = metadata;
  candidate_.audio = audio; candidate_.subtitle = subtitle; candidate_.paused = paused;
}
bool AutomaticRecoverySession::accept(Key key, std::int64_t entry)
{
  if (!same(candidate_.key, key) || entry <= 0 || accepted_) return false;
  candidate_.entry = entry; accepted_ = true; return true;
}
bool AutomaticRecoverySession::position(Key key, std::int64_t entry, std::int64_t positionMs)
{
  if (!accepted_ || !loaded_ || !same(candidate_.key, key) || entry != candidate_.entry || positionMs < 0) return false;
  candidate_.positionMs = positionMs; return true;
}
bool AutomaticRecoverySession::loaded(Key key, std::int64_t entry)
{
  if (!accepted_ || loaded_ || !same(candidate_.key, key) || entry != candidate_.entry) return false;
  loaded_ = true; return true;
}
bool AutomaticRecoverySession::revise(Key previous, Key current, bool paused)
{
  if (!same(candidate_.key, previous) || current.session != previous.session ||
      current.generation != previous.generation || current.revision <= previous.revision) return false;
  candidate_.key = current; candidate_.paused = paused; candidate_.positionMs = -1;
  return true;
}
bool AutomaticRecoverySession::tracks(Key current, const QVariant& audio, const QVariant& subtitle)
{
  if (!same(candidate_.key, current) || !accepted_ || !loaded_) return false;
  candidate_.audio = audio; candidate_.subtitle = subtitle; return true;
}
bool AutomaticRecoverySession::replaceEntry(Key current, std::int64_t previous, std::int64_t entry, std::int64_t positionMs)
{
  if (!same(candidate_.key, current) || !accepted_ || !loaded_ || candidate_.entry != previous ||
      entry <= 0 || entry == previous || positionMs < 0) return false;
  candidate_.entry = entry; candidate_.positionMs = positionMs; return true;
}
void AutomaticRecoverySession::invalidate()
{
  candidate_ = Snapshot{}; accepted_ = false; loaded_ = false;
}
bool AutomaticRecoverySession::snapshot(Key current, Snapshot& result) const
{
  if (!accepted_ || !loaded_ || !same(candidate_.key, current) || candidate_.entry <= 0 || candidate_.positionMs < 0) return false;
  result = candidate_; return true;
}
