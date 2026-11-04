#pragma once
#include <atomic>

// Linked only by the diagnostic build. Zero means use the real libmpv call.
struct DiagnosticFailureInjection
{
  static std::atomic<int> create;
  static std::atomic<int> render;
  static std::atomic<bool> blockNextRetire;
  static std::atomic<int> frees;
  static std::atomic<int> callbacks;
  static std::atomic<bool> holdCallback;
  static std::atomic<bool> callbackWaiting;
  static std::atomic<int> order;
  static std::atomic<int> closedOrder;
  static std::atomic<int> unregisteredOrder;
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
};
