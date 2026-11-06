#pragma once
#include <atomic>
struct mpv_render_context;

namespace HealthyRetirement {
// Shared independently of the QObject; post-free cleanup must not touch item memory.
struct RenderContextState {
  std::atomic<mpv_render_context*> context{nullptr};
};
}
