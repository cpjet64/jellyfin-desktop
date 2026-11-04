#pragma once

#include "helper.h"
#include <QMutex>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QQuickRenderControl>
#include <QQuickWindow>
#include <QSemaphore>
#include <QThread>
#include <QTimer>
#include <functional>
#include <QtQuick/private/qquickrendercontrol_p.h>
#include <QtQuick/private/qquickwindow_p.h>
#include <QtQuick/private/qsgcontext_p.h>

#if QT_VERSION != QT_VERSION_CHECK(6, 9, 3)
#error "Qt threaded diagnostic requires matching Qt 6.9.3 private headers"
#endif

namespace QtThreaded {
struct Scene::Data {
  explicit Data(FailurePoint point) : failure(point) {}
  FailurePoint failure;
  QThread thread;
  QObject executor;
  QQuickWindow* window = nullptr;
  QQuickRenderControl* control = nullptr;
  QOffscreenSurface* surface = nullptr;
  QOpenGLContext* context = nullptr;
  QOpenGLFramebufferObject* target = nullptr;
  QTimer* timer = nullptr;
  mutable QMutex lock;
  Audit state;
  QList<Frame> history;
  bool started = false;
  bool transferred = false;
  bool synchronized = false;
  bool initializeGui(QString* error);
  void initializeWorker();
  bool dispatch(const std::function<void()>& action, QString* error);
  void render();
  void cleanup();
  void fail(const QString& message);
};
}
