#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include <memory>

class QQuickItem;

namespace QtThreaded {
enum class FailurePoint { None, WrongVersion, BeforeTransfer, AfterTransfer, AfterRhiCreated, AfterInit, BlockCleanup };
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
  quint64 rhiCreatedCount = 0, rhiDestroyedCount = 0, cleanupCount = 0;
  quint64 controlDestroyedCount = 0, contextDestroyedCount = 0, surfaceDestroyedCount = 0;
  quint64 requestsAdmitted = 0, requestsCompleted = 0, requestsCancelled = 0;
  quint64 framesStarted = 0, framesCompleted = 0;
  bool privateBoundaryEntered = false;
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
  QList<Frame> frames() const;
  Audit audit() const;
  bool shutdown(QString* error);
private:
  struct Data;
  std::unique_ptr<Data> data;
};
}
