#pragma once

#include "frame_acceptance.h"
#include <QImage>
#include <cstdint>
#include <vector>

namespace fixture_restart {
// Fixture keys describe expected ownership, not an identity supplied by mpv.
struct CaptureTicket { FrameKey key; std::uint64_t generation = 0; };
void armCapture(const FrameKey &key, std::uintptr_t renderer);
void disarmCapture();
CaptureTicket captureTicket(std::uintptr_t renderer);
bool captureOverflowed();
std::uint64_t nextRenderSerial();
std::uint64_t currentRenderSerial();
void publishCapture(const CaptureTicket &ticket, QImage image, std::uint64_t updateFlags,
                    int infoResult, std::uint64_t nextFlags, int renderResult,
                    std::uint64_t context, std::uint64_t serial);
std::vector<Capture> takeCaptures();
}
