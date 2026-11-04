#pragma once

#include <atomic>
#include <chrono>
#include <thread>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include "c2/DiagnosticFailureInjection.h"

// The release worker never depends on GUI event processing.
class C2aCallbackHold
{
public:
  enum class Outcome { Pending, Observed, Timeout, Cleanup };
  ~C2aCallbackHold() { release(Outcome::Cleanup); join(); snapshotGui(); }
  void configure(const QString& directory, quintptr renderThread, quint64 generation)
  { evidence = directory; renderThreadBeforeArm = renderThread; selectedGeneration = generation; }
  void arm() { DiagnosticFailureInjection::holdCallback = true; }
  bool entered() const { return DiagnosticFailureInjection::callbackWaiting.load(); }
  void observeAndRelease(quint64 generation)
  {
    worker = std::thread([this, generation] {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
      startedNs = nowNs();
      deadlineNs = startedNs.load() + 8000000000LL;
      while (std::chrono::steady_clock::now() < deadline && outcome.load() == Outcome::Pending) {
        if (DiagnosticFailureInjection::unregisterEnteredOrder.load() > 0) {
          observedGeneration = DiagnosticFailureInjection::callbackGeneration.load();
          observedThread = DiagnosticFailureInjection::callbackThread.load();
          observedUnregisterThread = DiagnosticFailureInjection::unregisterThread.load();
          heldAtUnregister = DiagnosticFailureInjection::holdCallback.load() && entered() &&
            observedGeneration == generation && DiagnosticFailureInjection::frees.load() == 0 &&
            DiagnosticFailureInjection::ackOrder.load() == 0;
          snapshotWorker(Outcome::Observed);
          release(Outcome::Observed);
          return;
        }
        std::this_thread::yield();
      }
      snapshotWorker(Outcome::Timeout);
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
  void snapshotGui()
  {
    if (!guiSnapshotAttempted.exchange(true)) writeSnapshot("c2a-active-gui-snapshot.json", outcome.load());
  }
  void markRequestEntered() { requestEnteredNs = nowNs(); }
  void markRequestReturned() { requestReturnedNs = nowNs(); }
  void markAck() { ackNs = nowNs(); }
  std::atomic<Outcome> outcome{Outcome::Pending};
  std::atomic<bool> heldAtUnregister{false};
  std::atomic<quint64> observedGeneration{0};
  std::atomic<quintptr> observedThread{0}, observedUnregisterThread{0};
private:
  static qint64 nowNs()
  {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
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
        {"callback_waiting", DiagnosticFailureInjection::callbackWaiting.load()},
        {"selected_generation", qint64(selectedGeneration)},
        {"callback_generation", qint64(DiagnosticFailureInjection::callbackGeneration.load())},
        {"callback_thread", QString::number(DiagnosticFailureInjection::callbackThread.load(), 16)},
        {"render_thread_before_arm", QString::number(renderThreadBeforeArm, 16)},
        {"unregister_thread", QString::number(DiagnosticFailureInjection::unregisterThread.load(), 16)},
        {"worker_start_ns", startedNs.load()}, {"worker_deadline_ns", deadlineNs.load()},
        {"snapshot_ns", nowNs()}, {"request_entered_ns", requestEnteredNs.load()},
        {"request_returned_ns", requestReturnedNs.load()}, {"ack_ns", ackNs.load()},
        {"closed_order", DiagnosticFailureInjection::closedOrder.load()},
        {"callback_entered_order", DiagnosticFailureInjection::callbackEnteredOrder.load()},
        {"callback_hold_left_order", DiagnosticFailureInjection::callbackHoldLeftOrder.load()},
        {"callback_accounted_order", DiagnosticFailureInjection::callbackExitedOrder.load()},
        {"unregister_entered_order", DiagnosticFailureInjection::unregisterEnteredOrder.load()},
        {"unregister_returned_order", DiagnosticFailureInjection::unregisteredOrder.load()},
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
  std::thread worker;
  QString evidence;
  quintptr renderThreadBeforeArm = 0;
  quint64 selectedGeneration = 0;
  std::atomic<bool> guiSnapshotAttempted{false}, workerSnapshotOk{false};
  std::atomic<qint64> startedNs{0}, deadlineNs{0}, requestEnteredNs{0}, requestReturnedNs{0}, ackNs{0};
};
