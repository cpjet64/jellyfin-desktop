#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

// Diagnostic-only, bounded observation. It never calls PlayerComponent or mpv.
class PlayerIntentObservation
{
public:
  enum class Kind { LoadEnter, StopEnter, QueueDispatch, StopResult, Exit };
  enum class Outcome { None, GuardRejected, Unavailable, Error, Accepted };
  struct Event { std::uint64_t sequence = 0, invocation = 0, parent = 0; Kind kind = Kind::Exit;
    Outcome outcome = Outcome::None; int error = 0; };
  struct Snapshot { std::array<Event, 256> events{}; std::size_t count = 0; bool healthy = true; };

  class Scope {
  public:
    explicit Scope(Kind kind) noexcept : id_(begin(kind)) {}
    ~Scope() noexcept { end(id_); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    std::uint64_t id() const noexcept { return id_; }
  private:
    std::uint64_t id_;
  };

  static bool reset() noexcept
  {
    auto& s = state();
    if (s.lock.test_and_set()) { s.bad = true; return false; }
    const bool idle = s.active == 0;
    if (idle) { s.data = Snapshot{}; s.next = 0; s.bad = false; }
    else s.bad = true;
    s.lock.clear();
    return idle;
  }

  static Snapshot snapshot() noexcept
  {
    auto& s = state();
    if (s.lock.test_and_set()) { s.bad = true; Snapshot result; result.healthy = false; return result; }
    Snapshot result = s.data;
    result.healthy = !s.bad.load() && s.active == 0;
    s.lock.clear();
    return result;
  }

  static void queueDispatch() noexcept { appendCurrent(Kind::QueueDispatch, Outcome::None, 0); }
  static void stopResult(Outcome result, int error = 0) noexcept { appendCurrent(Kind::StopResult, result, error); }

private:
  struct State { std::atomic_flag lock = ATOMIC_FLAG_INIT; std::atomic<bool> bad{false};
    Snapshot data; std::uint64_t next = 0; std::size_t active = 0; };
  struct Stack { std::array<std::uint64_t, 8> ids{}; std::array<Kind, 8> kinds{}; std::size_t depth = 0; };
  static State& state() noexcept { static State value; return value; }
  static Stack& stack() noexcept { static thread_local Stack value; return value; }
  static void append(State& s, Kind kind, std::uint64_t id, std::uint64_t parent, Outcome outcome, int error) noexcept
  {
    if (s.data.count == s.data.events.size()) { s.bad = true; return; }
    const auto index = s.data.count++;
    s.data.events[index] = Event{index + 1, id, parent, kind, outcome, error};
  }
  static std::uint64_t begin(Kind kind) noexcept
  {
    auto& s = state(); auto& local = stack();
    if (s.lock.test_and_set()) { s.bad = true; return 0; }
    if (local.depth == local.ids.size()) { s.bad = true; s.lock.clear(); return 0; }
    if (local.depth && (local.depth != 1 || local.kinds[0] != Kind::LoadEnter || kind != Kind::StopEnter)) s.bad = true;
    const auto parent = local.depth ? local.ids[local.depth - 1] : 0;
    const auto id = ++s.next;
    local.ids[local.depth] = id;
    local.kinds[local.depth++] = kind;
    ++s.active;
    append(s, kind, id, parent, Outcome::None, 0);
    s.lock.clear();
    return id;
  }
  static void end(std::uint64_t id) noexcept
  {
    if (!id) return;
    auto& s = state(); auto& local = stack();
    if (s.lock.test_and_set()) { s.bad = true; return; }
    if (!local.depth || local.ids[local.depth - 1] != id) s.bad = true;
    else {
      --local.depth;
      append(s, Kind::Exit, id, local.depth ? local.ids[local.depth - 1] : 0, Outcome::None, 0);
      --s.active;
    }
    s.lock.clear();
  }
  static void appendCurrent(Kind kind, Outcome outcome, int error) noexcept
  {
    auto& s = state(); auto& local = stack();
    if (s.lock.test_and_set()) { s.bad = true; return; }
    const auto id = local.depth ? local.ids[local.depth - 1] : 0;
    const auto parent = local.depth > 1 ? local.ids[local.depth - 2] : 0;
    append(s, kind, id, parent, outcome, error);
    s.lock.clear();
  }
};
