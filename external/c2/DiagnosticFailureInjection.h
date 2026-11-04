#pragma once
#include <atomic>
#include <cstdint>

// Linked only by the diagnostic build. Zero means use the real libmpv call.
struct DiagnosticFailureInjection
{
  static std::int64_t monotonicNanos();
  static std::atomic<std::int64_t> failurePublishedNs;
  static std::atomic<std::int64_t> unregisterEnteredNs;
  static std::atomic<std::int64_t> unregisterReturnedNs;
  static std::atomic<std::int64_t> freeEnteredNs;
  static std::atomic<std::int64_t> freeReturnedNs;
  static std::atomic<std::int64_t> ackEmittedNs;
  static std::atomic<int> create;
  static std::atomic<int> render;
  static std::atomic<bool> blockNextRetire;
  static std::atomic<int> frees;
  static std::atomic<int> callbacks;
  static std::atomic<bool> holdCallback;
  static std::atomic<bool> callbackWaiting;
  static std::atomic<int> order;
  static std::atomic<int> closedOrder;
  static std::atomic<int> callbackEnteredOrder;
  static std::atomic<std::uint64_t> callbackGeneration;
  static std::atomic<std::uintptr_t> callbackThread;
  static std::atomic<int> callbackHoldLeftOrder;
  // Callback accounting completed; this is not a literal C++ return stamp.
  static std::atomic<int> callbackExitedOrder;
  static std::atomic<int> unregisterEnteredOrder;
  static std::atomic<std::uintptr_t> unregisterThread;
  static std::atomic<int> unregisteredOrder;
  static std::atomic<std::uintptr_t> freeThread;
  static std::atomic<std::uintptr_t> freeCurrentContext;
  static std::atomic<std::uintptr_t> freeOriginalContext;
  static std::atomic<std::uint64_t> freeGeneration;
  static std::atomic<int> freedOrder;
  static std::atomic<int> clearedOrder;
  static std::atomic<int> ackOrder;
  static std::atomic<int> itemOrder;
  static std::atomic<int> coreOrder;
  static std::atomic<int> updatesQueued;
  static std::atomic<int> updatesDelivered;
  static std::atomic<int> updatesRejected;
  static std::atomic<bool> deferNextQueuedUpdate;
  static std::atomic<int> updatesDeferred;
  static std::atomic<int> deferredUpdatesRejected;
  static std::atomic<bool> holdDeferredUpdate;
  static std::atomic<int> ownersDestroyed;
};
