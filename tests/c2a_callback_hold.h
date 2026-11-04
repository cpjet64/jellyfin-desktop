#pragma once

#include <atomic>
#include <chrono>
#include <thread>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include "c2/DiagnosticFailureInjection.h"
#include "c2a_hold_gate.h"
#include "mpv_thread_probe.h"

// The release worker never depends on GUI event processing.
class C2aCallbackHold
{
public:
  using Outcome = C2aHoldGate::Outcome;
  C2aCallbackHold() : gate(DiagnosticFailureInjection::holdCallback,
    [] { return DiagnosticFailureInjection::unregisterEnteredOrder.load() > 0; },
    [this](Outcome decision) { captureDecision(decision); },
    [this](Outcome decision) { snapshotWorker(decision); }), outcome(gate.outcome) {}
  ~C2aCallbackHold() { gate.cancel(); join(); snapshotGui(); }
  void configure(const QString& directory, quintptr renderThread, quint64 generation,
                 std::shared_ptr<MpvRenderObservation> renderObservation)
  { evidence = directory; renderThreadBeforeArm = renderThread; selectedGeneration = generation; observedRender = std::move(renderObservation); }
  bool arm() { return gate.arm(); }
  bool ready() const { return gate.ready.load(); }
  bool released() const { return outcome.load() != Outcome::Pending; }
  bool entered() const { return DiagnosticFailureInjection::callbackWaiting.load(); }
  void observeAndRelease(quint64 generation)
  {
    selectedGeneration = generation;
    startedNs = nowNs();
    gate.start(std::chrono::seconds(8));
  }
  void join() { gate.join(); }
  void snapshotGui()
  {
    if (!guiSnapshotAttempted.exchange(true)) writeSnapshot("c2a-active-gui-snapshot.json", outcome.load());
  }
  bool snapshotBeforeAssert() { return writeSnapshot("c2a-active-gui-preassert.json", outcome.load()); }
  bool workerSnapshotSucceeded() const { return workerSnapshotOk.load(); }
  void markRequestEntered() { requestEnteredNs = nowNs(); }
  void markRequestReturned() { requestReturnedNs = nowNs(); }
  void markAck(quint64 generation)
  {
    ackNs = nowNs();
    ackGeneration = generation;
    ackThread = reinterpret_cast<quintptr>(QThread::currentThreadId());
    ++ackCount;
  }
private:
  C2aHoldGate gate;
public:
  std::atomic<Outcome>& outcome;
  std::atomic<bool> heldAtUnregister{false};
  std::atomic<quint64> observedGeneration{0};
  std::atomic<quintptr> observedThread{0}, observedUnregisterThread{0};
private:
  static qint64 nowNs()
  {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  void captureDecision(Outcome decision)
  {
    capturedHold = DiagnosticFailureInjection::holdCallback.load();
    capturedWaiting = DiagnosticFailureInjection::callbackWaiting.load();
    capturedUnregisterEnteredNs = DiagnosticFailureInjection::unregisterEnteredNs.load();
    capturedUnregisterReturnedNs = DiagnosticFailureInjection::unregisterReturnedNs.load();
    capturedFreeEnteredNs = DiagnosticFailureInjection::freeEnteredNs.load();
    capturedFreeReturnedNs = DiagnosticFailureInjection::freeReturnedNs.load();
    capturedAckEmittedNs = DiagnosticFailureInjection::ackEmittedNs.load();
    if (observedRender) {
      renderEnteredSequence = observedRender->enteredSequence.load();
      renderReturnedSequence = observedRender->returnedSequence.load();
      renderEnteredNs = observedRender->enteredNs.load();
      renderReturnedNs = observedRender->returnedNs.load();
      renderThread = observedRender->enteredThread.load();
      renderContext = observedRender->enteredContext.load();
    }
    observedGeneration = DiagnosticFailureInjection::callbackGeneration.load();
    observedThread = DiagnosticFailureInjection::callbackThread.load();
    observedUnregisterThread = DiagnosticFailureInjection::unregisterThread.load();
    heldAtUnregister = decision == Outcome::Observed && DiagnosticFailureInjection::holdCallback.load() && entered() &&
      observedGeneration == selectedGeneration && DiagnosticFailureInjection::frees.load() == 0 &&
      DiagnosticFailureInjection::ackOrder.load() == 0;
  }
  void snapshotWorker(Outcome decision)
  {
    workerSnapshotOk = writeSnapshot("c2a-active-worker-snapshot.json", decision);
  }
  bool writeSnapshot(const char* name, Outcome decision) noexcept
  {
    try {
      if (evidence.isEmpty()) return false;
      QJsonObject record{{"decision", int(decision)}, {"outcome", int(outcome.load())},
        {"held_at_unregister", heldAtUnregister.load()}, {"hold", DiagnosticFailureInjection::holdCallback.load()},
        {"hold_at_decision", capturedHold}, {"waiting_at_decision", capturedWaiting},
        {"render_entered_sequence", qint64(renderEnteredSequence)},
        {"render_returned_sequence", qint64(renderReturnedSequence)},
        {"render_entered_ns", renderEnteredNs}, {"render_returned_ns", renderReturnedNs},
        {"render_thread", QString::number(renderThread, 16)}, {"render_context", QString::number(renderContext, 16)},
        {"render_fields_are_independent_samples", true},
        {"decision_unregister_entered_ns", qint64(capturedUnregisterEnteredNs)},
        {"decision_unregister_returned_ns", qint64(capturedUnregisterReturnedNs)},
        {"decision_free_entered_ns", qint64(capturedFreeEnteredNs)},
        {"decision_free_returned_ns", qint64(capturedFreeReturnedNs)},
        {"decision_ack_emitted_ns", qint64(capturedAckEmittedNs)},
        {"callback_waiting", DiagnosticFailureInjection::callbackWaiting.load()},
        {"selected_generation", qint64(selectedGeneration)},
        {"callback_generation", qint64(DiagnosticFailureInjection::callbackGeneration.load())},
        {"callback_thread", QString::number(DiagnosticFailureInjection::callbackThread.load(), 16)},
        {"render_thread_before_arm", QString::number(renderThreadBeforeArm, 16)},
        {"unregister_thread", QString::number(DiagnosticFailureInjection::unregisterThread.load(), 16)},
        {"worker_start_ns", startedNs.load()}, {"worker_deadline_ns", qint64(gate.deadlineNs.load())},
        {"snapshot_ns", nowNs()}, {"request_entered_ns", requestEnteredNs.load()},
        {"request_returned_ns", requestReturnedNs.load()}, {"ack_ns", ackNs.load()},
        {"ack_generation", qint64(ackGeneration.load())}, {"ack_count", ackCount.load()},
        {"ack_thread", QString::number(ackThread.load(), 16)},
        {"closed_order", DiagnosticFailureInjection::closedOrder.load()},
        {"callback_entered_order", DiagnosticFailureInjection::callbackEnteredOrder.load()},
        {"callback_hold_left_order", DiagnosticFailureInjection::callbackHoldLeftOrder.load()},
        {"callback_accounted_order", DiagnosticFailureInjection::callbackExitedOrder.load()},
        {"unregister_entered_order", DiagnosticFailureInjection::unregisterEnteredOrder.load()},
        {"unregister_returned_order", DiagnosticFailureInjection::unregisteredOrder.load()},
        {"unregister_entered_ns", qint64(DiagnosticFailureInjection::unregisterEnteredNs.load())},
        {"unregister_returned_ns", qint64(DiagnosticFailureInjection::unregisterReturnedNs.load())},
        {"free_entered_ns", qint64(DiagnosticFailureInjection::freeEnteredNs.load())},
        {"free_returned_ns", qint64(DiagnosticFailureInjection::freeReturnedNs.load())},
        {"ack_emitted_ns", qint64(DiagnosticFailureInjection::ackEmittedNs.load())},
        {"frees", DiagnosticFailureInjection::frees.load()}, {"free_order", DiagnosticFailureInjection::freedOrder.load()},
        {"clear_order", DiagnosticFailureInjection::clearedOrder.load()}, {"ack_order", DiagnosticFailureInjection::ackOrder.load()},
        {"item_order", DiagnosticFailureInjection::itemOrder.load()}, {"core_order", DiagnosticFailureInjection::coreOrder.load()},
        {"updates_deferred", DiagnosticFailureInjection::updatesDeferred.load()},
        {"deferred_rejected", DiagnosticFailureInjection::deferredUpdatesRejected.load()}};
      if (QByteArray(name) == "c2a-active-gui-snapshot.json")
        record.insert("worker_snapshot_ok", workerSnapshotOk.load());
      const QByteArray bytes = QJsonDocument(record).toJson(QJsonDocument::Compact);
      if (bytes.size() > 8192) return false;
      QFile file(evidence + "/" + name);
      return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
        file.write(bytes) == bytes.size() && file.flush();
    } catch (...) { return false; }
  }
  QString evidence;
  std::shared_ptr<MpvRenderObservation> observedRender;
  bool capturedHold = false, capturedWaiting = false;
  quint64 renderEnteredSequence = 0, renderReturnedSequence = 0;
  qint64 renderEnteredNs = 0, renderReturnedNs = 0;
  quintptr renderThread = 0, renderContext = 0;
  qint64 capturedUnregisterEnteredNs = 0, capturedUnregisterReturnedNs = 0;
  qint64 capturedFreeEnteredNs = 0, capturedFreeReturnedNs = 0, capturedAckEmittedNs = 0;
  quintptr renderThreadBeforeArm = 0;
  quint64 selectedGeneration = 0;
  std::atomic<bool> guiSnapshotAttempted{false}, workerSnapshotOk{false};
  std::atomic<qint64> startedNs{0}, requestEnteredNs{0}, requestReturnedNs{0}, ackNs{0};
  std::atomic<quint64> ackGeneration{0};
  std::atomic<quintptr> ackThread{0};
  std::atomic<int> ackCount{0};
};
