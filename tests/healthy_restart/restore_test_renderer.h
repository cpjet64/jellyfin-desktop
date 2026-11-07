#pragma once
#include "capture_capability.h"
#include <QImage>
#include <memory>
#include <optional>

// Exercises the actual bridge's renderer-side private operations without GL.
class HealthyRestoreRendererFixture final {
public:
  explicit HealthyRestoreRendererFixture(const std::shared_ptr<HealthyRetirement::Slot>& slot);
  bool initialize();
  bool queue();
  bool publish(int index);
  bool replaceFramebuffer();
private:
  static QImage image(int index);
  std::shared_ptr<HealthyRetirement::CaptureBridge> bridge_;
  std::optional<HealthyRetirement::CaptureBridge::Ticket> ticket_;
};
