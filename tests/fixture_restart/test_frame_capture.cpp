#include "frame_capture.h"
#include <cstdlib>

static void check(bool value) { if (!value) std::abort(); }

using namespace fixture_restart;

int main()
{
    const FrameKey first{1, 2, 3, 4, 5, 6};
    const FrameKey second{1, 2, 3, 4, 7, 8};
    QImage image(160, 90, QImage::Format_RGBA8888);
    image.fill(QColor(12, 34, 56, 255));
    armCapture(first, 101);
    auto stale = captureTicket(101);
    armCapture(second, 102);
    check(!captureTicket(101).generation);
    publishCapture(stale, image, 1, 0, 2, 0, 9, nextRenderSerial());
    check(takeCaptures().empty());
    auto ticket = captureTicket(102);
    const auto serial = nextRenderSerial();
    publishCapture(ticket, image, 1, 0, 2, -3, 9, serial);
    image.fill(Qt::black);
    auto captures = takeCaptures();
    check(captures.size() == 1 && captures[0]->key == second);
    check(captures[0]->serial == serial && captures[0]->actualContext == 9);
    check(captures[0]->renderResult == -3 && captures[0]->rgba[0] == 12);
    for (int i = 0; i < 20; ++i)
        publishCapture(ticket, image, 0, 0, 0, 0, 9, nextRenderSerial());
    check(takeCaptures().empty() && captureOverflowed());
    disarmCapture();
    publishCapture(ticket, image, 0, 0, 0, 0, 9, nextRenderSerial());
    check(takeCaptures().empty() && !captureTicket(102).generation);
}
