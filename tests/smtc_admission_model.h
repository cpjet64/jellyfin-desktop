#pragma once

// Test-only model of the proposed contract; production does not use this.
#include <QMetaObject>
#include <QObject>
#include <memory>
#include <mutex>
#include <vector>

class SmtcModelReceiver : public QObject
{
public:
  explicit SmtcModelReceiver(std::vector<int>& values) : values(values) {}
  void dispatch(int value) { values.push_back(value); }
private:
  std::vector<int>& values;
};

struct SmtcAdmissionModel
{
  explicit SmtcAdmissionModel(SmtcModelReceiver* receiver) : owner(receiver) {}
  std::mutex mutex;
  SmtcModelReceiver* owner;
};

inline bool smtcModelEnqueue(const std::shared_ptr<SmtcAdmissionModel>& state, int button)
{
  std::lock_guard<std::mutex> lock(state->mutex);
  auto* receiver = state->owner;
  if (!receiver) return false;
  return QMetaObject::invokeMethod(receiver, [state, receiver, button] {
    {
      std::lock_guard<std::mutex> guard(state->mutex);
      if (state->owner != receiver) return;
    }
    receiver->dispatch(button);
  }, Qt::QueuedConnection);
}

inline void smtcModelClose(const std::shared_ptr<SmtcAdmissionModel>& state)
{
  std::lock_guard<std::mutex> lock(state->mutex);
  state->owner = nullptr;
}
