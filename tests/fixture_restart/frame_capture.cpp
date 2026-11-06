#include "frame_capture.h"

#include <QThread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>

namespace fixture_restart {
namespace {
std::mutex mutex;
FrameKey activeKey;
std::uint64_t generation = 0;
std::atomic<std::uint64_t> serial{0};
bool armed = false;
bool overflowed = false;
std::uintptr_t activeRenderer = 0;
std::vector<Capture> pending;
constexpr std::size_t maxCaptures = 16;
}

void armCapture(const FrameKey &key, std::uintptr_t renderer)
{
    std::lock_guard<std::mutex> lock(mutex);
    activeKey = key;
    ++generation;
    pending.clear();
    activeRenderer = renderer;
    overflowed = false;
    armed = renderer != 0;
}

void disarmCapture()
{
    std::lock_guard<std::mutex> lock(mutex);
    armed = false;
    ++generation;
    pending.clear();
}

CaptureTicket captureTicket(std::uintptr_t renderer)
{
    std::lock_guard<std::mutex> lock(mutex);
    return armed && renderer == activeRenderer ? CaptureTicket{activeKey, generation} : CaptureTicket{};
}

bool captureOverflowed()
{
    std::lock_guard<std::mutex> lock(mutex);
    return overflowed;
}

std::uint64_t nextRenderSerial()
{
    return ++serial;
}

std::uint64_t currentRenderSerial()
{
    return serial.load();
}

void publishCapture(const CaptureTicket &ticket, QImage image, std::uint64_t updateFlags,
                    int infoResult, std::uint64_t nextFlags, int renderResult,
                    std::uint64_t context, std::uint64_t renderSerial)
{
    if (!ticket.generation || image.isNull() || image.width() != 160 || image.height() != 90)
        return;
    image = image.convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull()) return;
    auto record = std::make_shared<FrameRecord>();
    record->key = ticket.key;
    record->width = image.width();
    record->height = image.height();
    const auto stride = static_cast<std::size_t>(image.width()) * 4;
    record->rgba.resize(stride * static_cast<std::size_t>(image.height()));
    for (int row = 0; row < image.height(); ++row)
        std::memcpy(record->rgba.data() + stride * row, image.constScanLine(row), stride);
    record->updateFlags = updateFlags;
    record->infoResult = infoResult;
    record->nextFlags = nextFlags;
    record->renderResult = renderResult;
    record->thread = reinterpret_cast<std::uintptr_t>(QThread::currentThreadId());
    record->capturedAt = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    record->actualContext = context;
    std::lock_guard<std::mutex> lock(mutex);
    if (!armed || ticket.generation != generation || !(ticket.key == activeKey)) return;
    record->serial = renderSerial;
    if (pending.size() == maxCaptures) {
        overflowed = true;
        pending.clear();
        return;
    }
    if (overflowed) return;
    pending.push_back(std::move(record));
}

std::vector<Capture> takeCaptures()
{
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<Capture> result;
    result.swap(pending);
    return result;
}
}
