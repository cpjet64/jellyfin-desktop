#pragma once
#include <atomic>
struct mpv_render_context;

namespace HealthyRetirement {
// Shared independently of the QObject; post-free cleanup must not touch item memory.
struct RenderContextState {
  std::atomic<mpv_render_context*> context{nullptr};
  bool clear(mpv_render_context* expected) noexcept
  {
    return context.compare_exchange_strong(expected, nullptr);
  }
};
}
