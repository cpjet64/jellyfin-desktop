#pragma once

#include <QMutex>
#include <QOpenGLContext>
#include <QThread>
#include <QVector>
#include <atomic>
#include <memory>
#include "mpvrenderer.h"
#include "player/MpvVideoItem.h"
#include "qt_threaded/helper.h"

struct MpvRenderEvent {
  qint64 entered = 0;
  qint64 returned = 0;
  quintptr thread = 0;
  quintptr context = 0;
  quintptr renderer = 0;
};

struct MpvRenderObservation {
  std::atomic<quint64> enteredSequence{0}, returnedSequence{0};
  std::atomic<qint64> enteredNs{0}, returnedNs{0};
  std::atomic<quintptr> enteredThread{0}, enteredContext{0};
  QMutex mutex;
  QVector<MpvRenderEvent> events;
  void append(const MpvRenderEvent& event)
  {
    QMutexLocker lock(&mutex);
    events.append(event);
    if (events.size() > 512) events.removeFirst();
  }
  QVector<MpvRenderEvent> snapshot()
  {
    QMutexLocker lock(&mutex);
    return events;
  }
};

class ProbeRenderer final : public MpvRenderer
{
public:
  ProbeRenderer(MpvAbstractItem* item, std::shared_ptr<MpvRenderObservation> observation)
    : MpvRenderer(item), observed(std::move(observation)) {}
  void render() override
  {
    MpvRenderEvent event;
    event.entered = QtThreaded::monotonicNanos();
    event.thread = reinterpret_cast<quintptr>(QThread::currentThreadId());
    event.context = reinterpret_cast<quintptr>(QOpenGLContext::currentContext());
    event.renderer = reinterpret_cast<quintptr>(this);
    observed->enteredNs = event.entered;
    observed->enteredThread = event.thread;
    observed->enteredContext = event.context;
    const quint64 sequence = observed->enteredSequence.fetch_add(1) + 1;
    MpvRenderer::render();
    event.returned = QtThreaded::monotonicNanos();
    observed->returnedNs = event.returned;
    observed->returnedSequence = sequence;
    observed->append(event);
    update();
  }
private:
  std::shared_ptr<MpvRenderObservation> observed;
};

class ProbeItem final : public MpvVideoItem
{
public:
  explicit ProbeItem(QQuickItem* parent) : MpvVideoItem(parent), observed(std::make_shared<MpvRenderObservation>()) {}
  Renderer* createRenderer() const override { return new ProbeRenderer(const_cast<ProbeItem*>(this), observed); }
  std::shared_ptr<MpvRenderObservation> observed;
};
