#pragma once

#include <QMetaObject>
#include <QObject>
#include <memory>
#include <mutex>

// The retained state is inert after close and never owns the receiver.
class SmtcAdmission
{
public:
  using Dispatch = void (*)(QObject*, int);
  explicit SmtcAdmission(QObject* receiver) : m_receiver(receiver) {}

  static bool enqueue(const std::shared_ptr<SmtcAdmission>& state, int value, Dispatch dispatch)
  {
    std::lock_guard<std::mutex> lock(state->m_mutex);
    QObject* receiver = state->m_receiver;
    if (!receiver) return false;
    return QMetaObject::invokeMethod(receiver, [state, receiver, value, dispatch] {
      {
        std::lock_guard<std::mutex> guard(state->m_mutex);
        if (state->m_receiver != receiver) return;
      }
      dispatch(receiver, value);
    }, Qt::QueuedConnection);
  }

  void close()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_receiver = nullptr;
  }

private:
  std::mutex m_mutex;
  QObject* m_receiver;
};
