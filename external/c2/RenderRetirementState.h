#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>

// Diagnostic-only lifecycle primitive. The caller keeps this object alive until
// the pinned libmpv callback setter has returned and all callbacks have left.
class RenderRetirementState
{
public:
  enum class Stage { Create, Render };
  enum class Result { BlockedNoContext, Retired, Stale };
  using Action = std::function<void()>;

  explicit RenderRetirementState(std::uint64_t generation) : m_generation(generation) {}

  bool enterCallback()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_inflight;
    return m_open;
  }

  void leaveCallback()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (--m_inflight == 0) m_quiescent.notify_all();
  }

  bool accepts(std::uint64_t generation)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_open && generation == m_generation;
  }

  bool failOnce(Stage stage, int error)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_failed) return false;
    m_failed = true;
    m_open = false;
    m_stage = stage;
    m_error = error;
    return true;
  }

  bool request(std::uint64_t generation)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (generation != m_generation || m_retired) return false;
    m_open = false;
    return true;
  }

  Result retire(std::uint64_t generation, bool originalContextAndThread,
                const Action& unregister, const Action& freeContext)
  {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (generation != m_generation || m_retired || m_retiring || m_open) return Result::Stale;
      if (!originalContextAndThread) return Result::BlockedNoContext;
      m_retiring = true;
    }
    unregister(); // Never hold the callback's mutex while entering libmpv.
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_quiescent.wait(lock, [this] { return m_inflight == 0; });
    }
    freeContext();
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_retired = true;
    }
    return Result::Retired;
  }

private:
  const std::uint64_t m_generation;
  std::mutex m_mutex;
  std::condition_variable m_quiescent;
  unsigned m_inflight = 0;
  bool m_open = true;
  bool m_failed = false;
  bool m_retired = false;
  bool m_retiring = false;
  Stage m_stage = Stage::Create;
  int m_error = 0;
};
