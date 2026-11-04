#include "private_boundary.h"
#include <QCoreApplication>
#include <QEvent>
#include <QQuickRenderTarget>

namespace QtThreaded {
static quintptr currentThread() { return reinterpret_cast<quintptr>(QThread::currentThreadId()); }

void Scene::Data::cleanup()
{
  { QMutexLocker guard(&lock); state.admissionClosed = true; state.cleanupThread = currentThread(); }
  if (timer) { timer->stop(); delete timer; timer = nullptr; }
  { QMutexLocker guard(&lock); state.frameSettled = true; }
  if (failure == FailurePoint::BlockCleanup) { fail("injected blocked cleanup"); return; }
  if (!context || !surface || !context->makeCurrent(surface)) { fail("cleanup context unavailable"); return; }
  { QMutexLocker guard(&lock); state.contextCurrentAtCleanup = true; }
  auto* priv = QQuickRenderControlPrivate::get(control);
  if (priv->frameStatus == QQuickRenderControlPrivate::RecordingFrame) { fail("cleanup encountered active frame"); return; }
  { QMutexLocker guard(&lock); state.noActiveFrameAtCleanup = true; }
  auto* win = QQuickWindowPrivate::get(window);
  auto* savedContext = priv->rc;
  auto* savedRhi = priv->rhi;
  auto* savedCommand = priv->cb;
  if (priv->offscreenSurface != surface || (priv->rhi && !priv->ownRhi)) {
    fail("unexpected fallback or QRhi ownership"); return;
  }
  control->invalidate();
  priv->rc->invalidate();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  window->setRenderTarget(QQuickRenderTarget());
  win->ensureCustomRenderTarget();
  const auto& rt = win->redirect.rt;
  if (rt.rt.renderTarget || rt.res.texture || rt.res.renderBuffer || rt.res.rpDesc ||
      rt.implicitBuffers.depthStencil || rt.implicitBuffers.depthStencilTexture ||
      rt.implicitBuffers.multisampleTexture) { fail("Qt render target wrappers remain"); return; }
  { QMutexLocker guard(&lock); state.wrappersCleared = true; }
  if (!context->makeCurrent(surface)) { fail("cleanup could not restore context"); return; }
  delete target; target = nullptr;
  priv->offscreenSurface = nullptr;
  { QMutexLocker guard(&lock); state.fallbackDetached = true; }
  delete control; control = nullptr;
  { QMutexLocker guard(&lock); state.controlDestroyed = true; ++state.controlDestroyedCount; }
  if (win->renderControl || win->animationController || win->renderer || win->redirect.rt.rt.renderTarget) {
    fail("Qt window retained scene graph resources"); return;
  }
  if (win->context == savedContext) win->context = nullptr;
  if (win->rhi == savedRhi) win->rhi = nullptr;
  if (win->redirect.commandBuffer == savedCommand) win->redirect.commandBuffer = nullptr;
  if (win->context || win->rhi || win->redirect.commandBuffer) { fail("unexpected window aliases"); return; }
  { QMutexLocker guard(&lock); state.aliasesCleared = true; }
  context->doneCurrent();
  delete context; context = nullptr;
  { QMutexLocker guard(&lock); state.contextDestructionThread = currentThread();
    state.contextDestroyed = true; ++state.contextDestroyedCount; ++state.cleanupCount; }
  executor.moveToThread(QCoreApplication::instance()->thread());
}

bool Scene::shutdown(QString* error)
{
  if (!data->started) {
    if (data->control && data->surface &&
        QQuickRenderControlPrivate::get(data->control)->offscreenSurface == data->surface)
      QQuickRenderControlPrivate::get(data->control)->offscreenSurface = nullptr;
    if (data->control) { delete data->control; data->control = nullptr;
      data->state.controlDestroyed = true; ++data->state.controlDestroyedCount; }
    delete data->window; data->window = nullptr;
    if (data->context) { delete data->context; data->context = nullptr;
      data->state.contextDestroyed = true; data->state.contextDestructionThread = currentThread();
      ++data->state.contextDestroyedCount; }
    if (data->surface) { delete data->surface; data->surface = nullptr;
      data->state.surfaceDestroyed = true; data->state.surfaceDestructionThread = currentThread();
      ++data->state.surfaceDestroyedCount; }
    return true;
  }
  if (data->state.workerJoined) return true;
  if (!data->transferred || !data->dispatch([this] { data->cleanup(); }, error)) return false;
  { QMutexLocker guard(&data->lock); if (!data->state.cleanupCount) return false; }
  data->thread.quit();
  if (!data->thread.wait(10000)) { data->fail("render worker did not join"); return false; }
  { QMutexLocker guard(&data->lock); data->state.workerJoined = true; }
  delete data->window; data->window = nullptr;
  delete data->surface; data->surface = nullptr;
  { QMutexLocker guard(&data->lock); data->state.surfaceDestructionThread = currentThread();
    data->state.surfaceDestroyed = true; ++data->state.surfaceDestroyedCount; }
  return true;
}
}
