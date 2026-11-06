#pragma once
#include <capture_capability.h>
#include <frame_capture.h>
#include <QPainter>

// The capture bridge is real; only the renderer boundary is injected.
class TestCaptureCapability {
  using Bridge = HealthyRetirement::CaptureBridge;
  static constexpr HealthyRetirement::Identity identity{11, 12, 13};
  std::shared_ptr<Bridge> bridge_;
  static HealthyRetirement::FramebufferObservation fbo() { return {7, 160, 90, 0, 0, 0, true, true}; }
public:
  explicit TestCaptureCapability(const std::shared_ptr<HealthyRetirement::Slot>& slot) : bridge_(slot->captureBridge()) {}
  bool initialize() { return bridge_ && bridge_->rendererCreated(identity) && bridge_->framebuffer(identity, fbo()); }
  bool issue(bool publish)
  {
    if (!bridge_->framebufferCurrent(identity, fbo())) return false;
    const auto ticket = bridge_->issue(identity, fixture_restart::nextRenderSerial());
    if (!ticket.request) return false;
    if (publish) {
      QImage image(160, 90, QImage::Format_RGBA8888); image.fill(Qt::black);
      QPainter painter(&image);
      for (int bit = 0; bit < 9; ++bit) painter.fillRect(5 + bit * 16, 8, 12, 12, QColor(70, 70, 70));
      painter.fillRect(0, 35, 20, 20, QColor(220, 220, 220));
      painter.end();
      bridge_->publish(ticket, identity, image, 0, 0, 0, 0);
    }
    return true;
  }
};
