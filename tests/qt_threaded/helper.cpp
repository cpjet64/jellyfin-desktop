#include "private_boundary.h"
#include <QCoreApplication>
#include <QQuickGraphicsDevice>
#include <QQuickRenderTarget>
#include <QQuickItem>
#include <QSGRendererInterface>
#include <cstdlib>
#include <chrono>

namespace QtThreaded {
static quintptr currentThread() { return reinterpret_cast<quintptr>(QThread::currentThreadId()); }
qint64 monotonicNanos()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Scene::Data::fail(const QString& message)
{
  QMutexLocker guard(&lock);
  if (state.error.isEmpty()) state.error = message;
}

bool Scene::Data::dispatch(const std::function<void()>& action, QString* error)
{
  auto done = std::make_shared<QSemaphore>();
  {
    QMutexLocker guard(&lock);
    if (state.admissionClosed) {
      ++state.requestsCancelled;
      if (error) *error = "worker admission is closed";
      return false;
    }
    ++state.requestsAdmitted;
  }
  if (!QMetaObject::invokeMethod(&executor, [action, done] { action(); done->release(); }, Qt::QueuedConnection)) {
    fail("worker request could not be queued");
    if (error) *error = state.error;
    return false;
  }
  if (!done->tryAcquire(1, 10000)) {
    fail("worker request timed out; process must remain contained");
    if (error) *error = state.error;
    return false;
  }
  QMutexLocker guard(&lock);
  if (error) *error = state.error;
  return true;
}

Scene::Scene(FailurePoint failure) : data(std::make_unique<Data>(failure)) {}

Scene::~Scene()
{
  if (data->started && !data->state.workerJoined) std::_Exit(EXIT_FAILURE);
  if (!data->started) shutdown(nullptr);
}

bool Scene::Data::initializeGui(QString* error)
{
  state.guiThread = currentThread();
  if (failure == FailurePoint::WrongVersion || QString::fromLatin1(qVersion()) != QStringLiteral("6.9.3")) {
    if (error) *error = "Qt runtime must be exactly 6.9.3";
    return false;
  }
  if (failure == FailurePoint::BeforeTransfer) { if (error) *error = "injected before transfer"; return false; }
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  control = new QQuickRenderControl;
  window = new QQuickWindow(control);
  window->resize(640, 360);
  context = new QOpenGLContext;
  if (!context->create() || context->isOpenGLES()) { if (error) *error = "OpenGL context unavailable"; return false; }
  surface = new QOffscreenSurface;
  surface->setFormat(context->format());
  surface->create();
  auto* priv = QQuickRenderControlPrivate::get(control);
  if (!surface->isValid() || priv->window != window || priv->rhi || priv->offscreenSurface || priv->initialized) {
    if (error) *error = "invalid initial private state or surface";
    return false;
  }
  priv->offscreenSurface = surface;
  return true;
}

void Scene::Data::initializeWorker()
{
  { QMutexLocker guard(&lock); state.initThread = currentThread(); }
  if (failure == FailurePoint::AfterTransfer) { fail("injected after transfer"); return; }
  if (!context->makeCurrent(surface)) { fail("worker cannot make context current"); return; }
  if (failure == FailurePoint::AfterRhiCreated) { fail("partial-init injection is unavailable"); return; }
  target = new QOpenGLFramebufferObject(QSize(640, 360));
  if (!target->isValid()) { fail("framebuffer creation failed"); return; }
  window->setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(context));
  window->setRenderTarget(QQuickRenderTarget::fromOpenGLTexture(target->texture(), target->size()));
  const bool initialized = control->initialize();
  auto* priv = QQuickRenderControlPrivate::get(control);
  { QMutexLocker guard(&lock); state.rhiCreated = priv->rhi; state.rhiCreatedCount = priv->rhi ? 1 : 0; }
  if (!initialized) { fail("render control initialization failed"); return; }
  if (!priv->ownRhi || !priv->rhi || priv->offscreenSurface != surface) { fail("unexpected QRhi ownership"); return; }
  { QMutexLocker guard(&lock); state.initialized = true; state.targetReady = true; }
  if (failure == FailurePoint::AfterInit) { fail("injected after initialization"); return; }
  timer = new QTimer(&executor);
  QObject::connect(timer, &QTimer::timeout, &executor, [this] { render(); });
  timer->start(16);
}

bool Scene::initialize(QString* error)
{
  QString localError;
  if (!data->initializeGui(&localError)) {
    data->fail(localError);
    if (error) *error = localError;
    return false;
  }
  data->executor.moveToThread(&data->thread);
  data->thread.start();
  data->started = true;
  if (!data->dispatch([this] { QMutexLocker guard(&data->lock); data->state.workerReady = true;
                               data->state.renderThread = currentThread(); }, error)) return false;
  data->control->prepareThread(&data->thread);
  data->control->moveToThread(&data->thread);
  data->context->moveToThread(&data->thread);
  data->transferred = true;
  { QMutexLocker guard(&data->lock); data->state.transferred = true; }
  if (!data->dispatch([this] { data->initializeWorker(); }, error)) return false;
  QMutexLocker guard(&data->lock);
  if (error) *error = data->state.error;
  return data->state.error.isEmpty();
}

void Scene::Data::render()
{
  if (!synchronized) return;
  if (!context->makeCurrent(surface)) { fail("render context unavailable"); return; }
  control->beginFrame();
  control->render();
  control->endFrame();
  Frame frame{target->toImage(), 0, currentThread(), monotonicNanos()};
  QMutexLocker guard(&lock);
  frame.sequence = ++state.frames;
  state.renderThreadObserved = frame.renderThread;
  history.append(frame);
  while (history.size() > 8) history.removeFirst();
}

bool Scene::sync(QString* error)
{
  { QMutexLocker guard(&data->lock);
    if (data->state.admissionClosed || !data->state.initialized) {
      ++data->state.requestsCancelled;
      if (error) *error = "scene is not available for synchronization";
      return false;
    }
  }
  data->control->polishItems();
  return data->dispatch([this] {
    if (!data->context->makeCurrent(data->surface)) { data->fail("sync context unavailable"); return; }
    data->control->beginFrame();
    data->control->sync();
    data->control->render();
    data->control->endFrame();
    data->synchronized = true;
  }, error);
}

QQuickItem* Scene::contentItem() const { return data->window ? data->window->contentItem() : nullptr; }
QList<Frame> Scene::frames() const { QMutexLocker guard(&data->lock); return data->history; }
Audit Scene::audit() const { QMutexLocker guard(&data->lock); return data->state; }
}
