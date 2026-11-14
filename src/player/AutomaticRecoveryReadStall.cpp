#include "AutomaticRecoveryReadStall.h"

namespace {
bool eligible(const AutomaticRecoveryReadStall::Sample& sample)
{
  return sample.key.session && sample.key.revision && sample.key.generation && sample.entry > 0 && sample.eventEpoch &&
      sample.batchSequence && sample.sourceFrameSerial && sample.positionMs >= 0 && sample.observedMs >= 0 &&
      sample.nowMs >= sample.observedMs && sample.nowMs - sample.observedMs <= 2000 && sample.seekable && sample.video &&
      sample.buffering && !sample.loading && !sample.seeking && !sample.nativePaused && !sample.desiredPaused && !sample.eof &&
      !sample.permanentFailure && sample.errorAbsent && sample.visible && sample.exposed;
}
bool sameIdentity(const AutomaticRecoveryReadStall::Sample& a, const AutomaticRecoveryReadStall::Sample& b)
{
  return a.key.session == b.key.session && a.key.revision == b.key.revision && a.key.generation == b.key.generation &&
      a.entry == b.entry && a.eventEpoch == b.eventEpoch;
}
}

void AutomaticRecoveryReadStall::reset() { active_ = false; previous_ = {}; baselineMs_ = 0; }

AutomaticRecoveryReadStall::Result AutomaticRecoveryReadStall::observe(const Sample& sample)
{
  if (!sample.batchSequence || sample.batchSequence <= lastSequence_) return Result::None;
  lastSequence_ = sample.batchSequence;
  if (!eligible(sample)) { reset(); return Result::None; }
  const bool restart = !active_ || !sameIdentity(sample, previous_) ||
      sample.observedMs <= previous_.observedMs || sample.observedMs - previous_.observedMs > 2000 ||
      sample.positionMs != previous_.positionMs || sample.sourceFrameSerial != previous_.sourceFrameSerial;
  if (restart) { baselineMs_ = sample.observedMs; active_ = true; }
  previous_ = sample;
  return !restart && sample.observedMs - baselineMs_ >= 30000 ? Result::BufferingStall : Result::None;
}
