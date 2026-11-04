#pragma once

#include <QElapsedTimer>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QQuickGraphicsDevice>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QtTest/QtTest>
#include <cstdlib>
#include <memory>
#include "player/MpvVideoItem.h"

class Scene
{
public:
  bool init();
  bool frame();
  bool waitFor(const std::function<bool()>& predicate, int ms);
  bool destroyItem();
  ~Scene();
  QOpenGLContext context;
  QOffscreenSurface surface;
  std::unique_ptr<QQuickRenderControl> control;
  std::unique_ptr<QQuickWindow> window;
  std::unique_ptr<QOpenGLFramebufferObject> target;
  MpvVideoItem* item = nullptr;
  bool mayHaveContext = false;
  bool retired = false;
};

inline bool Scene::init()
{
  if (!context.create() || context.isOpenGLES()) return false;
  surface.setFormat(context.format());
  surface.create();
  if (!surface.isValid() || !context.makeCurrent(&surface)) return false;
  target = std::make_unique<QOpenGLFramebufferObject>(QSize(640, 360));
  if (!target->isValid()) return false;
  control = std::make_unique<QQuickRenderControl>();
  window = std::make_unique<QQuickWindow>(control.get());
  window->resize(640, 360);
  window->setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(&context));
  window->setRenderTarget(QQuickRenderTarget::fromOpenGLTexture(target->texture(), target->size()));
  item = new MpvVideoItem(window->contentItem());
  item->setParent(window->contentItem());
  item->setObjectName("video");
  item->setSize(QSizeF(640, 360));
  return control->initialize();
}

inline bool Scene::frame()
{
  if (!context.makeCurrent(&surface)) return false;
  control->polishItems();
  control->beginFrame();
  control->sync();
  control->render();
  control->endFrame();
  return true;
}

inline bool Scene::waitFor(const std::function<bool()>& predicate, int ms)
{
  QElapsedTimer deadline;
  deadline.start();
  while (!predicate() && deadline.elapsed() < ms) {
    if (!frame()) return false;
    QTest::qWait(20);
  }
  return predicate();
}

inline bool Scene::destroyItem()
{
  if (!retired || !context.makeCurrent(&surface)) return false;
  control->invalidate();
  if (!context.makeCurrent(&surface)) return false;
  delete item;
  item = nullptr;
  return true;
}

inline Scene::~Scene()
{
  if (mayHaveContext && !retired) std::_Exit(EXIT_FAILURE);
  if ((target || item) && !context.makeCurrent(&surface)) std::_Exit(EXIT_FAILURE);
  if (control) control->invalidate();
  if ((target || item) && !context.makeCurrent(&surface)) std::_Exit(EXIT_FAILURE);
  delete item;
  item = nullptr;
  window.reset();
  control.reset();
  target.reset();
  context.doneCurrent();
}
