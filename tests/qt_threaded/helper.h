#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include <memory>

class QQuickItem;

namespace QtThreaded {
enum class FailurePoint { None, WrongVersion, WrongPrivateState, BeforeTransfer, AfterTransfer, AfterRhiCreated, AfterInit, BlockCleanup, HoldReadback };
struct Frame { QImage image; quint64 sequence = 0; quintptr renderThread = 0; qint64 monotonicNs = 0; };
qint64 monotonicNanos();
struct Audit {
  quintptr guiThread = 0, renderThread = 0, initThread = 0, renderThreadObserved = 0;
  quintptr cleanupThread = 0, contextDestructionThread = 0, surfaceDestructionThread = 0;
  quintptr rhiCleanupThread = 0;
  quint64 frames = 0;
  bool workerReady = false, transferred = false, rhiCreated = false, initialized = false;
  bool admissionClosed = false, frameSettled = false, contextCurrentAtCleanup = false;
  bool wrappersCleared = false, fallbackDetached = false, controlDestroyed = false;
  bool aliasesCleared = false, contextDestroyed = false, workerJoined = false, surfaceDestroyed = false;
  bool targetReady = false;
  bool affinitiesVerified = false, noActiveFrameAtCleanup = false;
  bool partialRhiVerified = false;
  quint64 rhiCreatedCount = 0, rhiDestroyedCount = 0, cleanupCount = 0;
  quint64 controlDestroyedCount = 0, contextDestroyedCount = 0, surfaceDestroyedCount = 0;
  quint64 requestsAdmitted = 0, requestsCompleted = 0, requestsCancelled = 0;
  quint64 framesStarted = 0, framesCompleted = 0;
  bool framePumpPaused = false;
  quint64 pauseAcks = 0, resumeAcks = 0;
  quintptr pauseAckThread = 0, resumeAckThread = 0;
  quintptr pauseAckContext = 0, resumeAckContext = 0;
  quint64 readbacksAccepted = 0, readbacksCompleted = 0, readbacksCancelled = 0, readbacksRejected = 0;
  bool privateBoundaryEntered = false;
  bool privateStateRejected = false;
  quint64 frameSettledOrder = 0, wrappersClearedOrder = 0, fallbackDetachedOrder = 0;
  quint64 rhiCleanupOrder = 0, controlDestroyedOrder = 0, cleanupAckOrder = 0;
  quint64 workerJoinedOrder = 0, surfaceDestroyedOrder = 0;
  QString error;
};

class Scene
{
public:
  explicit Scene(FailurePoint failure = FailurePoint::None);
  ~Scene();
  bool initialize(QString* error);
  QQuickItem* contentItem() const;
  bool sync(QString* error);
  bool pauseFramePump(QString* error);
  bool resumeFramePump(QString* error);
  QList<Frame> frames() const;
  bool requestReadback();
  Audit audit() const;
  bool shutdown(QString* error);
private:
  struct Data;
  std::unique_ptr<Data> data;
};
}
