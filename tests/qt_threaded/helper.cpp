#include "private_boundary.h"
#include <QCoreApplication>
#include <QQuickGraphicsDevice>
#include <QQuickRenderTarget>
#include <QQuickItem>
#include <QSGRendererInterface>
#include <QLibraryInfo>
#include <QFileInfo>
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

bool Scene::Data::dispatch(const std::function<void()>& action, QString* error, bool allowClosed)
{
  auto done = std::make_shared<QSemaphore>();
  {
    QMutexLocker guard(&lock);
    if (state.admissionClosed && !allowClosed) {
      ++state.requestsCancelled;
      if (error) *error = "worker admission is closed";
      return false;
    }
    ++state.requestsAdmitted;
  }
  if (!QMetaObject::invokeMethod(&executor, [this, action, done] {
        action(); { QMutexLocker guard(&lock); ++state.requestsCompleted; } done->release();
      }, Qt::QueuedConnection)) {
    { QMutexLocker guard(&lock); ++state.requestsCancelled; }
    fail("worker request could not be queued");
    if (error) *error = state.error;
    return false;
  }
  if (!done->tryAcquire(1, 10000)) {
    std::_Exit(EXIT_FAILURE); // Never return to GUI mutations with unknown worker state.
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
  const QString expected = QFileInfo(QStringLiteral(QT_THREADED_QT_PREFIX)).canonicalFilePath();
  const QString actual = QFileInfo(QLibraryInfo::path(QLibraryInfo::PrefixPath)).canonicalFilePath();
  if (expected.isEmpty() || actual != expected) {
    if (error) *error = "Qt runtime prefix does not match the private header build";
    return false;
  }
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  control = new QQuickRenderControl;
  window = new QQuickWindow(control);
  window->resize(640, 360);
  context = new QOpenGLContext;
  if (!context->create() || context->isOpenGLES()) { if (error) *error = "OpenGL context unavailable"; return false; }
  surface = new QOffscreenSurface;
  surface->setFormat(context->format());
  surface->create();
  state.privateBoundaryEntered = true;
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
  if (priv->rhi && priv->ownRhi)
    priv->rhi->addCleanupCallback(this, [this](QRhi*) {
      QMutexLocker guard(&lock);
      state.rhiCleanupThread = currentThread();
      ++state.rhiDestroyedCount;
    });
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
  auto ready = std::make_shared<QSemaphore>();
  QObject::connect(&data->thread, &QThread::started, &data->thread, [ready] { ready->release(); }, Qt::DirectConnection);
  data->thread.start();
  data->started = true;
  if (!ready->tryAcquire(1, 10000)) std::_Exit(EXIT_FAILURE);
  { QMutexLocker guard(&data->lock); data->state.workerReady = true; }
  if (data->failure == FailurePoint::BeforeTransfer) {
    data->fail("injected before transfer");
    if (error) *error = "injected before transfer";
    return false;
  }
  data->executor.moveToThread(&data->thread);
  data->control->prepareThread(&data->thread);
  data->control->moveToThread(&data->thread);
  data->context->moveToThread(&data->thread);
  data->transferred = true;
  auto* priv = QQuickRenderControlPrivate::get(data->control);
  auto* win = QQuickWindowPrivate::get(data->window);
  const bool affinities = data->control->thread() == &data->thread && priv->rc->thread() == &data->thread &&
    win->animationController->thread() == &data->thread && data->context->thread() == &data->thread &&
    data->window->thread() == QCoreApplication::instance()->thread() &&
    data->window->contentItem()->thread() == QCoreApplication::instance()->thread();
  { QMutexLocker guard(&data->lock); data->state.transferred = true; data->state.affinitiesVerified = affinities; }
  if (!affinities) { data->fail("unexpected object affinity after transfer"); return false; }
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
  if (QQuickRenderControlPrivate::get(control)->frameStatus != QQuickRenderControlPrivate::RecordingFrame) {
    fail("beginFrame failed"); return;
  }
  { QMutexLocker guard(&lock); ++state.framesStarted; }
  control->render();
  control->endFrame();
  { QMutexLocker guard(&lock); ++state.framesCompleted; }
  Frame frame{target->toImage(), 0, currentThread(), monotonicNanos()};
  if (frame.image.isNull()) { fail("frame readback failed"); return; }
  QMutexLocker guard(&lock);
  frame.sequence = ++state.frames;
  state.renderThreadObserved = frame.renderThread;
  history.append(frame);
  while (history.size() > 8) history.removeFirst();
  if (pendingReadback && failure != FailurePoint::HoldReadback) {
    pendingReadback = false;
    ++state.readbacksCompleted;
  }
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
  auto done = std::make_shared<QSemaphore>();
  { QMutexLocker guard(&data->lock); ++data->state.requestsAdmitted; }
  const bool queued = QMetaObject::invokeMethod(&data->executor, [this, done] {
    if (!data->context->makeCurrent(data->surface)) {
      data->fail("sync context unavailable");
      { QMutexLocker guard(&data->lock); ++data->state.requestsCompleted; }
      done->release(); return;
    }
    data->control->beginFrame();
    if (QQuickRenderControlPrivate::get(data->control)->frameStatus != QQuickRenderControlPrivate::RecordingFrame) {
      data->fail("sync beginFrame failed");
      { QMutexLocker guard(&data->lock); ++data->state.requestsCompleted; }
      done->release(); return;
    }
    { QMutexLocker guard(&data->lock); ++data->state.framesStarted; }
    data->control->sync();
    data->synchronized = true;
    { QMutexLocker guard(&data->lock); ++data->state.requestsCompleted; }
    done->release(); // GUI may resume after sync, while rendering continues.
    data->control->render();
    data->control->endFrame();
    { QMutexLocker guard(&data->lock); ++data->state.framesCompleted; }
  }, Qt::QueuedConnection);
  if (!queued) { { QMutexLocker guard(&data->lock); ++data->state.requestsCancelled; }
    data->fail("sync request could not be queued"); return false; }
  if (!done->tryAcquire(1, 10000)) std::_Exit(EXIT_FAILURE);
  QMutexLocker guard(&data->lock);
  if (error) *error = data->state.error;
  return data->state.error.isEmpty();
}

QQuickItem* Scene::contentItem() const { return data->window ? data->window->contentItem() : nullptr; }
QList<Frame> Scene::frames() const { QMutexLocker guard(&data->lock); return data->history; }
Audit Scene::audit() const { QMutexLocker guard(&data->lock); return data->state; }
bool Scene::requestReadback()
{
  QMutexLocker guard(&data->lock);
  if (data->state.admissionClosed || !data->state.initialized || data->pendingReadback) {
    ++data->state.readbacksRejected;
    return false;
  }
  data->pendingReadback = true;
  ++data->state.readbacksAccepted;
  return true;
}
}
