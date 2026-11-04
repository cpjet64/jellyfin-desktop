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
  static std::atomic<int> createCallCount;
  static std::atomic<std::int64_t> createCallEnteredNs;
  static std::atomic<std::int64_t> createCallReturnedNs;
  static std::atomic<std::uint64_t> createCallGeneration;
  static std::atomic<std::uintptr_t> createCallThread;
  static std::atomic<std::uintptr_t> createCallContext;
  static std::atomic<int> createCallResult;
  static std::atomic<int> render;
  static std::atomic<int> servicePendingCount;
  static std::atomic<int> serviceSkipCount;
  static std::atomic<int> ordinaryPostFaultCount;
  static std::atomic<std::int64_t> servicePendingNs;
  static std::atomic<std::int64_t> serviceSkipEnteredNs;
  static std::atomic<std::int64_t> serviceSkipReturnedNs;
  static std::atomic<std::uint64_t> serviceGeneration;
  static std::atomic<std::uintptr_t> serviceThread;
  static std::atomic<std::uintptr_t> serviceContext;
  static std::atomic<int> serviceResult;
  static std::atomic<int> serviceCloseOrder;
  static std::atomic<std::int64_t> serviceCloseNs;
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
