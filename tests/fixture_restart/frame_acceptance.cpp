#include "frame_acceptance.h"

#include <algorithm>
#include <cmath>

namespace fixture_restart {
namespace {
bool sameOwner(const FrameKey &a, const FrameKey &b)
{
    return a.process == b.process && a.core == b.core && a.context == b.context
        && a.entry == b.entry && a.process && a.core && a.context && a.entry;
}

int brightness(const FrameRecord &r, int x, int y)
{
    const auto p = (y * r.width + x) * 4;
    return (int(r.rgba[p]) + int(r.rgba[p + 1]) + int(r.rgba[p + 2])) / 3;
}

bool region(const FrameRecord &r, int x, int y, int w, int h, int low, int high)
{
    int valid = 0, count = 0;
    for (int row = y + 2; row < y + h - 2; ++row)
        for (int col = x + 2; col < x + w - 2; ++col) {
            const int v = brightness(r, col, row);
            valid += v >= low && v <= high;
            ++count;
        }
    return count && valid * 100 >= count * 90;
}

bool usable(const FrameRecord &r)
{
    return r.serial && r.thread && r.actualContext && r.infoResult == 0 && r.renderResult == 0
        && r.width == 160 && r.height == 90 && r.rgba.size() == 160 * 90 * 4;
}

bool grayscaleOpaque(const FrameRecord &r)
{
    int valid = 0;
    for (std::size_t p = 0; p < r.rgba.size(); p += 4) {
        const auto lo = std::min({r.rgba[p], r.rgba[p + 1], r.rgba[p + 2]});
        const auto hi = std::max({r.rgba[p], r.rgba[p + 1], r.rgba[p + 2]});
        valid += hi - lo <= 24 && r.rgba[p + 3] >= 250;
    }
    return valid * 100 >= 160 * 90 * 98;
}
}

bool FrameKey::operator==(const FrameKey &o) const
{
    return sameOwner(*this, o) && phase == o.phase && seek == o.seek;
}

std::optional<int> decodeFrame(const FrameRecord &r)
{
    if (!usable(r) || !grayscaleOpaque(r) || !region(r, 0, 60, 160, 20, 0, 85))
        return std::nullopt;
    int index = 0;
    for (int bit = 0; bit < 9; ++bit) {
        const int x = 5 + bit * 16;
        const bool low = region(r, x, 8, 12, 12, 25, 115);
        const bool high = region(r, x, 8, 12, 12, 165, 255);
        if (low == high)
            return std::nullopt;
        if (high)
            index |= 1 << bit;
    }
    if (index > 299)
        return std::nullopt;
    const int x = (index * 3) % 140;
    if (!region(r, x, 35, 20, 20, 150, 255))
        return std::nullopt;
    for (int col = 2; col < 158; col += 4)
        if ((col < x || col >= x + 20) && brightness(r, col, 45) > 110)
            return std::nullopt;
    return index;
}

bool FrameAcceptance::baseline(Capture record)
{
    invalidate();
    if (!record || !sameOwner(record->key, record->key))
        return false;
    const auto index = decodeFrame(*record);
    if (!index || *index > 2)
        return false;
    baseline_ = std::make_shared<const FrameRecord>(*record);
    return true;
}

bool FrameAcceptance::arm(const FrameKey &key, std::uint64_t afterSerial, int checkpointMs)
{
    target_.reset();
    armed_ = baseline_ && sameOwner(baseline_->key, key) && key.phase && key.seek
        && afterSerial >= baseline_->serial && checkpointMs >= 2900 && checkpointMs <= 3100;
    key_ = key;
    boundary_ = afterSerial;
    checkpointMs_ = checkpointMs;
    seek_ = restart_ = snapshot_ = false;
    return armed_;
}

void FrameAcceptance::capture(Capture record)
{
    if (!armed_ || !record || !(record->key == key_) || record->serial <= boundary_
        || record->thread != baseline_->thread || record->actualContext != baseline_->actualContext)
        return;
    const auto index = decodeFrame(*record);
    const auto base = decodeFrame(*baseline_);
    if (index && base && *index != *base && std::abs(*index * 1000 - checkpointMs_ * 30) <= 3000)
        target_ = std::make_shared<const FrameRecord>(*record);
}

void FrameAcceptance::acknowledge(const FrameKey &key, bool seek, bool restart, bool snapshot)
{
    if (armed_ && key == key_) {
        seek_ |= seek;
        if (seek_)
            restart_ |= restart;
        if (restart_)
            snapshot_ |= snapshot;
    }
}

void FrameAcceptance::invalidate()
{
    baseline_.reset();
    target_.reset();
    armed_ = seek_ = restart_ = snapshot_ = false;
}

bool FrameAcceptance::accepted() const
{
    return armed_ && target_ && seek_ && restart_ && snapshot_;
}

Capture FrameAcceptance::target() const
{
    return accepted() ? target_ : Capture{};
}
}
