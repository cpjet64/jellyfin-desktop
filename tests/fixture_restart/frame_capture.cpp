#include "frame_capture.h"
#include "capture_capability.h"

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
std::uint64_t qualifiedRequest = 0, qualifiedRenderer = 0;
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
    qualifiedRequest = qualifiedRenderer = 0;
    overflowed = false;
    armed = renderer != 0;
}

void disarmCapture()
{
    std::lock_guard<std::mutex> lock(mutex);
    armed = false;
    qualifiedRequest = qualifiedRenderer = 0;
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
    constexpr qsizetype rowBytes = 160 * 4;
    if (image.isNull() || !image.constBits() || image.bytesPerLine() < rowBytes ||
        image.sizeInBytes() < image.bytesPerLine() * 89 + rowBytes) return;
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

namespace HealthyRetirement {
void CaptureBridge::publish(const Ticket& ticket, Identity identity, QImage image, std::uint64_t updateFlags,
                            int infoResult, std::uint64_t nextFlags, int renderResult)
{
    if (!ticket.request || image.isNull() || image.width() != 160 || image.height() != 90) return;
    image = image.convertToFormat(QImage::Format_RGBA8888);
    constexpr qsizetype rowBytes = 160 * 4;
    if (image.isNull() || !image.constBits() || image.bytesPerLine() < rowBytes ||
        image.sizeInBytes() < image.bytesPerLine() * 89 + rowBytes) return;
    auto record = std::make_shared<fixture_restart::FrameRecord>();
    record->key = ticket.key; record->serial = ticket.serial;
    record->width = 160; record->height = 90;
    record->rgba.resize(static_cast<std::size_t>(rowBytes) * 90);
    for (int row = 0; row < 90; ++row)
        std::memcpy(record->rgba.data() + rowBytes * row, image.constScanLine(row), rowBytes);
    record->updateFlags = updateFlags; record->infoResult = infoResult;
    record->nextFlags = nextFlags; record->renderResult = renderResult;
    record->thread = identity.thread; record->actualContext = identity.context;
    record->capturedAt = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    if (!consume(ticket, identity)) return;
    record->captureEvidence = evidence(ticket, identity, *record);
    if (!record->captureEvidence) return;
    std::lock_guard<std::mutex> lock(fixture_restart::mutex);
    if (fixture_restart::qualifiedRequest != ticket.request || fixture_restart::qualifiedRenderer != ticket.renderer) {
        fixture_restart::pending.clear();
        fixture_restart::overflowed = false;
        fixture_restart::qualifiedRequest = ticket.request;
        fixture_restart::qualifiedRenderer = ticket.renderer;
    }
    if (fixture_restart::overflowed) return;
    if (fixture_restart::pending.size() == fixture_restart::maxCaptures) {
        fixture_restart::overflowed = true;
        fixture_restart::pending.clear();
        return;
    }
    fixture_restart::pending.push_back(std::move(record));
}
}
