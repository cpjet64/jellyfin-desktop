#include "AutomaticRecoveryLoad.h"

bool AutomaticRecoveryLoad::matches(Key current) const
{
  return key_.session && key_.revision && key_.generation && key_.session == current.session &&
      key_.revision == current.revision && key_.generation == current.generation;
}
bool AutomaticRecoveryLoad::begin(Key key, std::uint64_t sequence, std::int64_t deadlineMs)
{
  if (state_ == State::Pending || !key.session || !key.revision || !key.generation ||
      !sequence || sequence <= sequence_ || sequence > MaxSequence || deadlineMs <= 0) return false;
  key_ = key; sequence_ = sequence; deadline_ = deadlineMs; replyEntry_ = startedEntry_ = 0;
  replySeen_ = startSeen_ = false; state_ = State::Pending; return true;
}
void AutomaticRecoveryLoad::settle()
{
  if (replySeen_ && startSeen_) state_ = replyEntry_ == startedEntry_ ? State::Accepted : State::Failed;
}
void AutomaticRecoveryLoad::reply(std::uint64_t userdata, bool ok, std::int64_t entry, std::int64_t nowMs)
{
  if (state_ != State::Pending || userdata != this->userdata()) return;
  if (nowMs < 0 || nowMs >= deadline_) { state_ = State::Expired; return; }
  if (replySeen_ || !ok || entry <= 0) { state_ = State::Failed; return; }
  replySeen_ = true; replyEntry_ = entry; settle();
}
void AutomaticRecoveryLoad::started(std::int64_t entry, std::int64_t nowMs)
{
  if (state_ == State::Accepted) { state_ = State::Failed; return; }
  if (state_ != State::Pending) return;
  if (nowMs < 0 || nowMs >= deadline_) { state_ = State::Expired; return; }
  if (startSeen_ || entry <= 0) { state_ = State::Failed; return; }
  startSeen_ = true; startedEntry_ = entry; settle();
}
void AutomaticRecoveryLoad::ended(std::int64_t entry)
{
  if ((state_ == State::Pending || state_ == State::Accepted) &&
      (entry <= 0 || entry == replyEntry_ || entry == startedEntry_)) state_ = State::Failed;
}
AutomaticRecoveryLoad::State AutomaticRecoveryLoad::poll(Key current, std::int64_t nowMs)
{
  if ((state_ == State::Pending || state_ == State::Accepted) && !matches(current)) state_ = State::Failed;
  if (state_ == State::Pending && (nowMs < 0 || nowMs >= deadline_)) state_ = State::Expired;
  return state_;
}
void AutomaticRecoveryLoad::invalidate() { state_ = State::Failed; }
