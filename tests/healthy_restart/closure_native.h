#pragma once
#include <atomic>
#include <cstdint>
class QThread;
struct mpv_handle;

namespace HealthyRetirement {
// Sole owner of the transferred worker and core. The graph retains this state
// until finalization, including indefinitely when any native step is uncertain.
class NativeTeardownState {
public:
  NativeTeardownState(QThread* worker, mpv_handle* core, QThread* gui);
  ~NativeTeardownState();
  NativeTeardownState(const NativeTeardownState&) = delete;
  NativeTeardownState& operator=(const NativeTeardownState&) = delete;
  void quit();
  bool wait(std::int64_t remainingNanoseconds);
  void destroy();
  bool finalize();
private:
  QThread* worker_;
  mpv_handle* core_;
  QThread* const gui_;
  std::atomic_bool joined_{false}, destroying_{false}, destroyed_{false};
};
}
