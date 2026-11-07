#include "restore_test_renderer.h"
#include "frame_capture.h"
#include <QPainter>

namespace {
constexpr HealthyRetirement::Identity identity{11, 12, 13};
HealthyRetirement::FramebufferObservation fbo(std::uint64_t attachment = 7)
{ return {attachment, 160, 90, 0, 0, 0, true, true}; }
}

HealthyRestoreRendererFixture::HealthyRestoreRendererFixture(const std::shared_ptr<HealthyRetirement::Slot>& slot)
  : bridge_(slot ? slot->captureBridge() : nullptr) {}

bool HealthyRestoreRendererFixture::initialize()
{ return bridge_ && bridge_->rendererCreated(identity) && bridge_->framebuffer(identity, fbo()); }

bool HealthyRestoreRendererFixture::queue()
{
  if (!bridge_ || !bridge_->framebufferCurrent(identity, fbo())) return false;
  ticket_ = bridge_->issue(identity, fixture_restart::nextRenderSerial());
  return ticket_->request != 0;
}

bool HealthyRestoreRendererFixture::publish(int index)
{
  if (!bridge_ || !ticket_ || index < 0 || index > 299) return false;
  bridge_->publish(*ticket_, identity, image(index), 0, 0, 0, 0);
  ticket_.reset();
  return true;
}

bool HealthyRestoreRendererFixture::replaceFramebuffer()
{ return bridge_ && bridge_->framebuffer(identity, fbo(8)); }

QImage HealthyRestoreRendererFixture::image(int index)
{
  QImage result(160, 90, QImage::Format_RGBA8888); result.fill(Qt::black);
  QPainter painter(&result);
  for (int bit = 0; bit < 9; ++bit) {
    const int value = index & (1 << bit) ? 220 : 70;
    painter.fillRect(5 + bit * 16, 8, 12, 12, QColor(value, value, value));
  }
  painter.fillRect((index * 3) % 140, 35, 20, 20, QColor(220, 220, 220));
  return result;
}
