#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
namespace HealthyRetirement { class CaptureEvidence; }

namespace fixture_restart {

// Expected fixture ownership, not an identity supplied by mpv.
struct FrameKey {
    std::uint64_t process = 0, core = 0, context = 0, entry = 0;
    std::uint64_t phase = 0, seek = 0;
    bool operator==(const FrameKey &other) const;
};

// RGBA pixels are tightly packed, top row first, copied on the render thread.
// Fixture output is grayscale and opaque; acknowledgement inputs must already
// reflect the broker's checked seek, restart and position-qualified snapshot.
struct FrameRecord {
    std::shared_ptr<const HealthyRetirement::CaptureEvidence> captureEvidence;
    FrameKey key;
    std::uint64_t serial = 0;
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
    int updateResult = 0, infoResult = 0, renderResult = 0;
    std::uint64_t updateFlags = 0, nextFlags = 0;
    std::uint64_t thread = 0, actualContext = 0, capturedAt = 0; // steady_clock nanoseconds
};

using Capture = std::shared_ptr<const FrameRecord>;
std::optional<int> decodeFrame(const FrameRecord &record);

class FrameAcceptance {
public:
    bool baseline(Capture record);
    bool arm(const FrameKey &key, std::uint64_t afterSerial, int checkpointMs);
    void capture(Capture record);
    void acknowledge(const FrameKey &key, bool seek, bool restart, bool snapshot);
    void invalidate();
    bool accepted() const;
    Capture target() const;

private:
    Capture baseline_, target_;
    FrameKey key_;
    std::uint64_t boundary_ = 0;
    int checkpointMs_ = 0;
    bool armed_ = false, seek_ = false, restart_ = false, snapshot_ = false;
};
}
