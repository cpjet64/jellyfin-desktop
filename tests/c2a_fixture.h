#pragma once

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QThread>
#include <QUrl>
#include <MpvController>
#include "c2a_finalizer.h"

// GUI-thread fixture. The supplied binding predicate is defined by the friend
// test class; this helper does not expose PlayerComponent internals.
class C2aFixture
{
public:
  C2aFixture() : cleanup(scene) {}
  bool initialize(PlayerComponent& player, QString* error)
  {
    if (!player.componentInitialize() || !scene.initialize(error)) return false;
    item = new ProbeItem(scene.contentItem());
    item->setObjectName("video");
    item->setSize(QSizeF(640, 360));
    controller = item->controller();
    core = controller ? controller->mpv() : nullptr;
    if (!core) return false;
    generation = item->renderGeneration();
    ready = std::make_unique<QSignalSpy>(item, &MpvAbstractItem::ready);
    failure = std::make_unique<QSignalSpy>(item, &MpvAbstractItem::renderFailure);
    cleanup.beginCreate(item);
    if (!scene.sync(error)) return false;
    QElapsedTimer timer;
    timer.start();
    while (ready->isEmpty() && failure->isEmpty() && timer.elapsed() < 15000) {
      QCoreApplication::processEvents();
      QThread::msleep(10);
    }
    if (ready->count() != 1 || !failure->isEmpty()) return false;
    cleanup.confirmLive();
    player.setWindow(scene.contentItem()->window());
    return true;
  }
  bool startPlayback(PlayerComponent& player, const QString& media)
  {
    return player.load(QUrl::fromLocalFile(media).toString(), {{"autoplay", true}}, {{"type", "video"}});
  }
  bool proveAdvancing(PlayerComponent& player)
  {
    QElapsedTimer timer;
    timer.start();
    qint64 firstPosition = -1;
    qint64 startedAt = 0;
    while (timer.elapsed() < 10000) {
      QCoreApplication::processEvents();
      const qint64 position = player.getPosition();
      if (position > 500 && firstPosition < 0) { firstPosition = position; startedAt = QtThreaded::monotonicNanos(); }
      const auto frames = scene.frames();
      for (const auto& first : frames) for (const auto& second : frames) {
        if (first.monotonicNs >= startedAt && startedAt > 0 && first.sequence < second.sequence &&
            second.monotonicNs <= QtThreaded::monotonicNanos() && visible(first.image) && visible(second.image) &&
            first.image != second.image && firstPosition >= 0 && position > firstPosition + 200) {
          playbackStartPosition = firstPosition;
          playbackEndPosition = position;
          playbackFirstFrame = first;
          playbackSecondFrame = second;
          return true;
        }
      }
      QThread::msleep(20);
    }
    return false;
  }
  QtThreaded::Scene scene;
  C2aFinalizer cleanup;
  ProbeItem* item = nullptr;
  MpvController* controller = nullptr;
  mpv_handle* core = nullptr;
  quint64 generation = 0;
  std::unique_ptr<QSignalSpy> ready;
  std::unique_ptr<QSignalSpy> failure;
  qint64 playbackStartPosition = -1;
  qint64 playbackEndPosition = -1;
  QtThreaded::Frame playbackFirstFrame;
  QtThreaded::Frame playbackSecondFrame;
private:
  static bool visible(const QImage& image)
  {
    if (image.isNull()) return false;
    int bright = 0;
    for (int y = 0; y < image.height(); y += 4)
      for (int x = 0; x < image.width(); x += 4) if (qGray(image.pixel(x, y)) > 170) ++bright;
    return bright > 100;
  }
};
