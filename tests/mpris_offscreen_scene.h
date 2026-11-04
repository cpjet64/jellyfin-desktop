#pragma once
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QQuickGraphicsDevice>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include "player/MpvVideoItem.h"

class MprisOffscreenScene
{
public:
  bool initialize();
  bool frame();
  QImage image();
  ~MprisOffscreenScene();
  QOpenGLContext context;
  QOffscreenSurface surface;
  std::unique_ptr<QQuickRenderControl> control;
  std::unique_ptr<QQuickWindow> window;
  std::unique_ptr<QOpenGLFramebufferObject> target;
  MpvVideoItem* video = nullptr;
  bool initialized = false;
  bool failed = false;
};

inline bool MprisOffscreenScene::initialize()
{
  context.setFormat(QSurfaceFormat::defaultFormat());
  if (!context.create() || context.isOpenGLES()) return false;
  surface.setFormat(context.format());
  surface.create();
  if (!surface.isValid() || !context.makeCurrent(&surface)) return false;
  const QSize size(640, 360);
  target = std::make_unique<QOpenGLFramebufferObject>(size);
  if (!target->isValid()) return false;
  control = std::make_unique<QQuickRenderControl>();
  window = std::make_unique<QQuickWindow>(control.get());
  QObject::connect(window.get(), &QQuickWindow::sceneGraphError, window.get(),
                   [this](QQuickWindow::SceneGraphError, const QString& message) {
    failed = true;
    qCritical() << "Scene graph error:" << message;
  }, Qt::DirectConnection);
  window->resize(size);
  window->setColor(Qt::black);
  window->setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(&context));
  window->setRenderTarget(QQuickRenderTarget::fromOpenGLTexture(target->texture(), size));
  video = new MpvVideoItem(window->contentItem());
  video->setParent(window->contentItem());
  video->setObjectName("video");
  video->setSize(QSizeF(size));
  initialized = control->initialize();
  return initialized && !failed && window->rendererInterface()->graphicsApi() == QSGRendererInterface::OpenGL;
}

inline bool MprisOffscreenScene::frame()
{
  if (failed || !context.makeCurrent(&surface)) return false;
  control->polishItems();
  control->beginFrame();
  control->sync();
  control->render();
  control->endFrame();
  return !failed;
}

inline QImage MprisOffscreenScene::image()
{
  if (!frame() || !context.makeCurrent(&surface)) return {};
  return target->toImage();
}

inline MprisOffscreenScene::~MprisOffscreenScene()
{
  if (!video && !target && !initialized) return;
  const auto requireContext = [this]() {
    if (!context.isValid() || !surface.isValid() || !context.makeCurrent(&surface)) {
      std::fputs("Cannot restore offscreen context for cleanup\n", stderr);
      std::fflush(stderr);
      std::_Exit(EXIT_FAILURE);
    }
  };
  requireContext();
  if (control && initialized) control->invalidate();
  requireContext();
  delete video;
  video = nullptr;
  requireContext();
  window.reset();
  requireContext();
  control.reset();
  requireContext();
  target.reset();
  context.doneCurrent();
}
