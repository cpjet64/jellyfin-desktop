#include "PlaybackIntent.h"

PlaybackIntent::PlaybackIntent(std::uint64_t epoch) noexcept
{
  state_.epoch = epoch && epoch <= MaxSafeInteger ? epoch : 0;
  if (!state_.epoch) state_.reason = Reason::CounterExhausted;
}

bool PlaybackIntent::advance(bool newRequest) noexcept
{
  if (state_.desired == Desired::Exiting) return false;
  if (!state_.epoch || state_.revision == MaxSafeInteger ||
      (newRequest && state_.request == MaxSafeInteger)) {
    state_.reason = Reason::CounterExhausted;
    state_.desired = Desired::Stopped;
    state_.hasSeek = false;
    state_.dispatch = Dispatch::None;
    state_.timelineQualified = false;
    return false;
  }
  ++state_.revision;
  if (newRequest) ++state_.request;
  state_.dispatch = Dispatch::None;
  state_.admission = Admission::Unclassified;
  state_.observation = Observation::Unknown;
  state_.timelineQualified = false;
  return true;
}

void PlaybackIntent::play(bool paused) noexcept
{
  if (!advance(true)) return;
  state_.desired = paused ? Desired::Paused : Desired::Playing;
  state_.reason = Reason::LegacyUnclassified;
  state_.hasSeek = false;
}

void PlaybackIntent::stop(Reason reason) noexcept
{
  if (!advance(false)) return;
  state_.desired = Desired::Stopped;
  state_.reason = reason;
  state_.hasSeek = false;
}

void PlaybackIntent::pause() noexcept
{
  if (!advance(false)) return;
  state_.desired = Desired::Paused;
  state_.reason = Reason::LegacyUnclassified;
}

void PlaybackIntent::resume() noexcept
{
  if (!advance(false)) return;
  state_.desired = Desired::Playing;
  state_.reason = Reason::LegacyUnclassified;
}

void PlaybackIntent::seek(std::int64_t milliseconds) noexcept
{
  if (!advance(false)) return;
  state_.hasSeek = milliseconds >= 0 && static_cast<std::uint64_t>(milliseconds) <= MaxSafeInteger;
  state_.seekMs = state_.hasSeek ? milliseconds : 0;
  state_.reason = Reason::LegacyUnclassified;
}

void PlaybackIntent::invalidate(Reason reason) noexcept
{
  if (!advance(false)) return;
  state_.reason = reason;
}

void PlaybackIntent::exit() noexcept
{
  if (state_.desired == Desired::Exiting) return;
  const bool advanced = advance(false);
  state_.desired = Desired::Exiting;
  if (advanced) state_.reason = Reason::Exiting;
  state_.hasSeek = false;
}

bool PlaybackIntent::dispatch(std::uint64_t revision, Dispatch result) noexcept
{
  if (!revision || revision != state_.revision || state_.reason == Reason::CounterExhausted ||
      state_.desired == Desired::Exiting) return false;
  state_.dispatch = result;
  return true;
}
