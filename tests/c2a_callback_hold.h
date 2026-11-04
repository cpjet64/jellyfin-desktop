#pragma once

#include <atomic>
#include <chrono>
#include <thread>
#include "c2/DiagnosticFailureInjection.h"

// The release worker never depends on GUI event processing.
class C2aCallbackHold
{
public:
  enum class Outcome { Pending, Observed, Timeout, Cleanup };
  ~C2aCallbackHold() { release(Outcome::Cleanup); join(); }
  void arm() { DiagnosticFailureInjection::holdCallback = true; }
  bool entered() const { return DiagnosticFailureInjection::callbackWaiting.load(); }
  void observeAndRelease(quint64 generation)
  {
    worker = std::thread([this, generation] {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
      while (std::chrono::steady_clock::now() < deadline && outcome.load() == Outcome::Pending) {
        if (DiagnosticFailureInjection::unregisterEnteredOrder.load() > 0) {
          observedGeneration = DiagnosticFailureInjection::callbackGeneration.load();
          observedThread = DiagnosticFailureInjection::callbackThread.load();
          observedUnregisterThread = DiagnosticFailureInjection::unregisterThread.load();
          heldAtUnregister = DiagnosticFailureInjection::holdCallback.load() && entered() &&
            observedGeneration == generation && DiagnosticFailureInjection::frees.load() == 0 &&
            DiagnosticFailureInjection::ackOrder.load() == 0;
          release(Outcome::Observed);
          return;
        }
        std::this_thread::yield();
      }
      release(Outcome::Timeout);
    });
  }
  void release(Outcome value)
  {
    Outcome expected = Outcome::Pending;
    outcome.compare_exchange_strong(expected, value);
    DiagnosticFailureInjection::holdCallback = false;
  }
  void join() { if (worker.joinable()) worker.join(); }
  std::atomic<Outcome> outcome{Outcome::Pending};
  std::atomic<bool> heldAtUnregister{false};
  std::atomic<quint64> observedGeneration{0};
  std::atomic<quintptr> observedThread{0}, observedUnregisterThread{0};
private:
  std::thread worker;
};
