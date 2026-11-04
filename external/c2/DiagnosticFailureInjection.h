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
};
