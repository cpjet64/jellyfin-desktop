#include "DiagnosticFailureInjection.h"
#include <chrono>

std::int64_t DiagnosticFailureInjection::monotonicNanos()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::atomic<std::int64_t> DiagnosticFailureInjection::failurePublishedNs{0};
std::atomic<std::int64_t> DiagnosticFailureInjection::unregisterEnteredNs{0};
std::atomic<std::int64_t> DiagnosticFailureInjection::unregisterReturnedNs{0};
std::atomic<std::int64_t> DiagnosticFailureInjection::freeEnteredNs{0};
std::atomic<std::int64_t> DiagnosticFailureInjection::freeReturnedNs{0};
std::atomic<std::int64_t> DiagnosticFailureInjection::ackEmittedNs{0};
std::atomic<int> DiagnosticFailureInjection::create{0};
std::atomic<int> DiagnosticFailureInjection::createCallCount{0};
std::atomic<std::int64_t> DiagnosticFailureInjection::createCallEnteredNs{0};
std::atomic<std::int64_t> DiagnosticFailureInjection::createCallReturnedNs{0};
std::atomic<std::uint64_t> DiagnosticFailureInjection::createCallGeneration{0};
std::atomic<std::uintptr_t> DiagnosticFailureInjection::createCallThread{0};
std::atomic<std::uintptr_t> DiagnosticFailureInjection::createCallContext{0};
std::atomic<int> DiagnosticFailureInjection::createCallResult{0};
std::atomic<int> DiagnosticFailureInjection::render{0};
std::atomic<int> DiagnosticFailureInjection::servicePendingCount{0};
std::atomic<int> DiagnosticFailureInjection::serviceEntryCount{0};
std::atomic<std::int64_t> DiagnosticFailureInjection::serviceEntryFirstNs{0}, DiagnosticFailureInjection::serviceEntryLastNs{0};
std::atomic<std::uint64_t> DiagnosticFailureInjection::serviceEntryFirstGeneration{0}, DiagnosticFailureInjection::serviceEntryLastGeneration{0};
std::atomic<std::uintptr_t> DiagnosticFailureInjection::serviceEntryFirstThread{0}, DiagnosticFailureInjection::serviceEntryLastThread{0};
std::atomic<int> DiagnosticFailureInjection::serviceUpdateCount{0};
std::atomic<int> DiagnosticFailureInjection::serviceFrameCount{0}, DiagnosticFailureInjection::serviceNoFrameCount{0};
std::atomic<std::uint64_t> DiagnosticFailureInjection::serviceUpdateFirstMask{0}, DiagnosticFailureInjection::serviceUpdateLastMask{0};
std::atomic<int> DiagnosticFailureInjection::serviceSkipCount{0};
std::atomic<int> DiagnosticFailureInjection::ordinaryPostFaultCount{0};
std::atomic<std::int64_t> DiagnosticFailureInjection::servicePendingNs{0};
std::atomic<std::int64_t> DiagnosticFailureInjection::serviceSkipEnteredNs{0};
std::atomic<std::int64_t> DiagnosticFailureInjection::serviceSkipReturnedNs{0};
std::atomic<std::uint64_t> DiagnosticFailureInjection::serviceGeneration{0};
std::atomic<std::uintptr_t> DiagnosticFailureInjection::serviceThread{0};
std::atomic<std::uintptr_t> DiagnosticFailureInjection::serviceContext{0};
std::atomic<int> DiagnosticFailureInjection::serviceResult{0};
std::atomic<int> DiagnosticFailureInjection::serviceCloseOrder{0};
std::atomic<std::int64_t> DiagnosticFailureInjection::serviceCloseNs{0};
std::atomic<bool> DiagnosticFailureInjection::blockNextRetire{false};
std::atomic<int> DiagnosticFailureInjection::frees{0};
std::atomic<int> DiagnosticFailureInjection::callbacks{0};
std::atomic<bool> DiagnosticFailureInjection::holdCallback{false};
std::atomic<bool> DiagnosticFailureInjection::callbackWaiting{false};
std::atomic<int> DiagnosticFailureInjection::order{0};
std::atomic<int> DiagnosticFailureInjection::closedOrder{0};
std::atomic<int> DiagnosticFailureInjection::callbackEnteredOrder{0};
std::atomic<std::uint64_t> DiagnosticFailureInjection::callbackGeneration{0};
std::atomic<std::uintptr_t> DiagnosticFailureInjection::callbackThread{0};
std::atomic<int> DiagnosticFailureInjection::callbackHoldLeftOrder{0};
std::atomic<int> DiagnosticFailureInjection::callbackExitedOrder{0};
std::atomic<int> DiagnosticFailureInjection::unregisterEnteredOrder{0};
std::atomic<std::uintptr_t> DiagnosticFailureInjection::unregisterThread{0};
std::atomic<int> DiagnosticFailureInjection::unregisteredOrder{0};
std::atomic<std::uintptr_t> DiagnosticFailureInjection::freeThread{0};
std::atomic<std::uintptr_t> DiagnosticFailureInjection::freeCurrentContext{0};
std::atomic<std::uintptr_t> DiagnosticFailureInjection::freeOriginalContext{0};
std::atomic<std::uint64_t> DiagnosticFailureInjection::freeGeneration{0};
std::atomic<int> DiagnosticFailureInjection::freedOrder{0};
std::atomic<int> DiagnosticFailureInjection::clearedOrder{0};
std::atomic<int> DiagnosticFailureInjection::ackOrder{0};
std::atomic<int> DiagnosticFailureInjection::itemOrder{0};
std::atomic<int> DiagnosticFailureInjection::coreOrder{0};
std::atomic<int> DiagnosticFailureInjection::updatesQueued{0};
std::atomic<int> DiagnosticFailureInjection::updatesDelivered{0};
std::atomic<int> DiagnosticFailureInjection::updatesRejected{0};
std::atomic<bool> DiagnosticFailureInjection::deferNextQueuedUpdate{false};
std::atomic<int> DiagnosticFailureInjection::updatesDeferred{0};
std::atomic<int> DiagnosticFailureInjection::deferredUpdatesRejected{0};
std::atomic<bool> DiagnosticFailureInjection::holdDeferredUpdate{false};
std::atomic<int> DiagnosticFailureInjection::ownersDestroyed{0};
