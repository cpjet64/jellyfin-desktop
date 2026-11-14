#include "AutomaticRecoveryObservation.h"
#include "AutomaticRecoveryNative.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace {
constexpr std::uint64_t Tag = std::uint64_t{1} << 60;
constexpr std::uint64_t MaxSequence = (std::uint64_t{1} << 49) - 1;
constexpr std::int64_t FreshnessMs = 2000; // Initial policy; runtime validation is required.
const char* const Names[] = {"playlist", "seekable", "seeking", "paused-for-cache", "pause", "time-pos", "vid", "playlist"};
}
std::uint64_t AutomaticRecoveryObservation::userdata(unsigned slot) const { return Tag | (sequence_ << 4) | slot; }
bool AutomaticRecoveryObservation::current(Key key, std::uint64_t epoch, std::int64_t now) const
{
  return key.session && key.revision && key.generation && key.session == key_.session && key.revision == key_.revision &&
      key.generation == key_.generation && epoch && epoch == epoch_ && now >= lastMs_ && now - startedMs_ < FreshnessMs;
}
bool AutomaticRecoveryObservation::request(const Dispatch& dispatch, unsigned slot, const char* name, mpv_format format)
{
  try { return dispatch && dispatch(userdata(slot), name, format) >= 0; } catch (...) { return false; }
}
bool AutomaticRecoveryObservation::begin(Key key, std::int64_t entry, std::uint64_t epoch, std::int64_t now,
                                         const Dispatch& dispatch)
{
  invalidate();
  if (!key.session || !key.revision || !key.generation || entry <= 0 || !epoch || now < 0 || sequence_ >= MaxSequence)
    return false;
  ++sequence_; key_ = key; epoch_ = epoch; startedMs_ = lastMs_ = now; result_.entry = entry;
  phase_ = Phase::Before;
  if (!request(dispatch, 0, Names[0], MPV_FORMAT_NODE)) { invalidate(); return false; }
  return true;
}
bool AutomaticRecoveryObservation::requestPayload(const Dispatch& dispatch)
{
  pending_ = 0;
  for (unsigned slot = 1; slot <= 6; ++slot) {
    const auto format = slot == 5 ? MPV_FORMAT_DOUBLE : slot == 6 ? MPV_FORMAT_INT64 : MPV_FORMAT_FLAG;
    if (!request(dispatch, slot, Names[slot], format)) return false;
    pending_ |= 1u << slot;
  }
  return true;
}
bool AutomaticRecoveryObservation::consumePayload(unsigned slot, const mpv_event_property& property)
{
  if (!property.data) return false;
  if (slot >= 1 && slot <= 4) {
    if (property.format != MPV_FORMAT_FLAG) return false;
    const int value = *static_cast<const int*>(property.data);
    if (value != 0 && value != 1) return false;
    if (slot == 1) result_.seekable = value != 0;
    if (slot == 2) result_.seeking = value != 0;
    if (slot == 3) result_.buffering = value != 0;
    if (slot == 4) result_.nativePaused = value != 0;
    return true;
  }
  if (slot == 6) {
    if (property.format != MPV_FORMAT_INT64) return false;
    result_.video = *static_cast<const std::int64_t*>(property.data) > 0; return true;
  }
  if (slot != 5 || property.format != MPV_FORMAT_DOUBLE) return false;
  const double value = *static_cast<const double*>(property.data) * 1000.0;
  if (!std::isfinite(value) || value < 0 || value > double(std::numeric_limits<std::int64_t>::max() / 2)) return false;
  result_.positionMs = static_cast<std::int64_t>(value); return true;
}
void AutomaticRecoveryObservation::observe(const mpv_event* event, Key key, std::uint64_t epoch, std::int64_t now,
                                           const Dispatch& dispatch)
{
  if (phase_ == Phase::Idle || phase_ == Phase::Failed || !event) return;
  if (!current(key, epoch, now)) { invalidate(); return; }
  lastMs_ = now;
  if (event->event_id != MPV_EVENT_GET_PROPERTY_REPLY) return;
  unsigned slot = 0;
  for (; slot < 8 && event->reply_userdata != userdata(slot); ++slot) {}
  if (slot == 8) return;
  if (event->error < 0 || !event->data) { invalidate(); return; }
  const auto& property = *static_cast<const mpv_event_property*>(event->data);
  if (!property.name || std::strcmp(property.name, Names[slot])) { invalidate(); return; }
  if ((phase_ == Phase::Before && slot == 0) || (phase_ == Phase::After && slot == 7)) {
    if (property.format != MPV_FORMAT_NODE || !property.data ||
        AutomaticRecoveryNative::playingEntry(*static_cast<const mpv_node*>(property.data)) != result_.entry) {
      invalidate(); return;
    }
    if (phase_ == Phase::After) { result_.observedMs = now; result_.batchSequence = sequence_; phase_ = Phase::Ready; return; }
    phase_ = Phase::Payload;
    if (!requestPayload(dispatch)) invalidate();
    return;
  }
  if (phase_ != Phase::Payload || slot < 1 || slot > 6 || !(pending_ & (1u << slot)) ||
      !consumePayload(slot, property)) { invalidate(); return; }
  pending_ &= ~(1u << slot);
  if (!pending_) {
    phase_ = Phase::After;
    if (!request(dispatch, 7, Names[7], MPV_FORMAT_NODE)) invalidate();
  }
}
bool AutomaticRecoveryObservation::take(Key key, std::uint64_t epoch, std::int64_t now, Result& result)
{
  if (phase_ != Phase::Ready || !current(key, epoch, now) || result_.positionMs < 0) { invalidate(); return false; }
  result = result_; invalidate(); return true;
}
void AutomaticRecoveryObservation::invalidate() { phase_ = Phase::Failed; pending_ = 0; result_ = {}; }
