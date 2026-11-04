#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

// One deadline covers both admission and retirement; close cannot be rearmed.
class C2aHoldGate
{
public:
  enum class Outcome { Pending, Observed, Timeout, Cancelled };
  using Clock = std::chrono::steady_clock;
  using Callback = std::function<void(Outcome)>;
  C2aHoldGate(std::atomic<bool>& held, std::function<bool()> observed,
              Callback capture, Callback afterRelease)
    : held(held), observed(std::move(observed)), capture(std::move(capture)), afterRelease(std::move(afterRelease)) {}
  ~C2aHoldGate() { cancel(); join(); }
  void start(std::chrono::milliseconds timeout)
  {
    worker = std::thread([this, timeout] {
      { std::lock_guard<std::mutex> lock(mutex); deadline = Clock::now() + timeout;
        deadlineNs = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline.time_since_epoch()).count();
        ready = true; }
      while (true) {
        Outcome decision = Outcome::Pending;
        {
          std::lock_guard<std::mutex> lock(mutex);
          if (outcome.load() != Outcome::Pending) return;
          if (Clock::now() >= deadline) decision = Outcome::Timeout;
          else if (armed && observed()) decision = Outcome::Observed;
          if (decision != Outcome::Pending) {
            try { capture(decision); } catch (...) { }
            held = false;
            outcome = decision;
          }
        }
        if (decision != Outcome::Pending) {
          try { afterRelease(decision); } catch (...) { }
          return;
        }
        std::this_thread::yield();
      }
    });
  }
  bool arm()
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (!ready || outcome.load() != Outcome::Pending || Clock::now() >= deadline || armed) return false;
    armed = true;
    held = true;
    return true;
  }
  void cancel()
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (outcome.load() == Outcome::Pending) {
      held = false;
      outcome = Outcome::Cancelled;
    }
  }
  void join() { if (worker.joinable()) worker.join(); }
  std::atomic<bool> ready{false};
  std::atomic<Outcome> outcome{Outcome::Pending};
  std::atomic<std::int64_t> deadlineNs{0};
private:
  std::atomic<bool>& held;
  std::function<bool()> observed;
  Callback capture, afterRelease;
  std::mutex mutex;
  Clock::time_point deadline;
  bool armed = false;
  std::thread worker;
};
