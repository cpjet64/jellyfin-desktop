#ifndef RENDER_STARTUP_DIAGNOSTICS_H
#define RENDER_STARTUP_DIAGNOSTICS_H

#include <QEvent>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <atomic>
#include <chrono>

#include "player/MpvVideoItem.h"
#include "windows_desktop_metadata.h"

class RenderStartupDiagnostics : public QObject
{
public:
  RenderStartupDiagnostics() : m_start(std::chrono::steady_clock::now()) {}

  void observe(QQuickWindow& window, MpvVideoItem& item)
  {
    window.installEventFilter(this);
    connect(&window, &QWindow::visibleChanged, &window,
            [this](bool visible) { if (visible) record(m_visible); }, Qt::DirectConnection);
    connect(&window, &QQuickWindow::sceneGraphInitialized, &window,
            [this]() { record(m_sceneGraph); }, Qt::DirectConnection);
    connect(&window, &QQuickWindow::sceneGraphError, &window,
            [this](QQuickWindow::SceneGraphError code, const QString& message) {
      record(m_errorTime);
      m_errorCode.store(static_cast<int>(code));
      if (m_mutex.tryLock()) {
        m_error = message;
        m_mutex.unlock();
      }
      m_failed.store(true);
    }, Qt::DirectConnection);
    connect(&window, &QQuickWindow::beforeSynchronizing, &window,
            [this]() { record(m_sync); }, Qt::DirectConnection);
    connect(&window, &QQuickWindow::beforeRendering, &window, [this]() {
      record(m_render);
      captureGl();
    }, Qt::DirectConnection);
    connect(&window, &QQuickWindow::frameSwapped, &window,
            [this]() { record(m_swap); }, Qt::DirectConnection);
    connect(&item, &MpvAbstractItem::ready, &window,
            [this]() { record(m_readyEmitted); }, Qt::DirectConnection);
    connect(&item, &MpvAbstractItem::ready, &window,
            [this]() { record(m_readyDelivered); }, Qt::QueuedConnection);
  }

  bool ready() const { return m_readyDelivered.load() >= 0; }
  bool failed() const { return m_failed.load(); }
  bool save(const QString& path, QQuickWindow& window, MpvVideoItem& item)
  {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const QByteArray data = QJsonDocument(snapshot(window, item)).toJson();
    return file.write(data) == data.size();
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override
  {
    Q_UNUSED(watched);
    if (event->type() == QEvent::Expose) record(m_expose);
    if (event->type() == QEvent::UpdateRequest) record(m_update);
    return false;
  }

private:
  void record(std::atomic<qint64>& value)
  {
    qint64 expected = -1;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - m_start).count();
    value.compare_exchange_strong(expected, elapsed);
  }

  void captureGl()
  {
    if (m_glCaptured.exchange(true)) return;
    if (!m_mutex.tryLock()) { m_glCaptured.store(false); return; }
    auto* context = QOpenGLContext::currentContext();
    m_glPresent = context != nullptr;
    m_glValid = context && context->isValid();
    if (!m_glValid) { m_mutex.unlock(); return; }
    const auto format = context->format();
    m_glFormat = QStringLiteral("%1.%2 profile=%3 renderable=%4")
      .arg(format.majorVersion()).arg(format.minorVersion()).arg(format.profile()).arg(format.renderableType());
    auto* functions = context->functions();
    auto string = [functions](GLenum name) {
      const auto* value = functions->glGetString(name);
      return value ? QString::fromLatin1(reinterpret_cast<const char*>(value)) : QString();
    };
    m_vendor = string(GL_VENDOR);
    m_renderer = string(GL_RENDERER);
    m_version = string(GL_VERSION);
    m_mutex.unlock();
  }

  QJsonObject snapshot(QQuickWindow& window, MpvVideoItem& item)
  {
    QJsonObject times{{"visible", m_visible.load()}, {"expose", m_expose.load()}, {"update", m_update.load()},
      {"sceneGraphInitialized", m_sceneGraph.load()}, {"beforeSynchronizing", m_sync.load()},
      {"sceneGraphError", m_errorTime.load()},
      {"beforeRendering", m_render.load()}, {"frameSwapped", m_swap.load()},
      {"readyEmitted", m_readyEmitted.load()}, {"readyDelivered", m_readyDelivered.load()}};
    QJsonObject windowInfo{{"visible", window.isVisible()}, {"exposed", window.isExposed()},
      {"width", window.width()}, {"height", window.height()}};
    if (auto* screen = window.screen()) {
      const QRect geometry = screen->geometry();
      windowInfo["screen"] = QJsonObject{{"name", screen->name()}, {"x", geometry.x()}, {"y", geometry.y()},
        {"width", geometry.width()}, {"height", geometry.height()}, {"devicePixelRatio", screen->devicePixelRatio()}};
    }
    QJsonObject itemInfo{{"visible", item.isVisible()}, {"opacity", item.opacity()},
      {"width", item.width()}, {"height", item.height()}, {"hasWindow", item.window() == &window}};
    QMutexLocker lock(&m_mutex);
    QJsonObject gl{{"captured", m_glCaptured.load()}, {"contextPresent", m_glPresent},
      {"valid", m_glValid}, {"format", m_glFormat},
      {"vendor", m_vendor}, {"renderer", m_renderer}, {"version", m_version}};
    QJsonObject environment{{"QSG_RENDER_LOOP", qEnvironmentVariable("QSG_RENDER_LOOP")},
      {"QT_QUICK_BACKEND", qEnvironmentVariable("QT_QUICK_BACKEND")},
      {"QT_OPENGL", qEnvironmentVariable("QT_OPENGL")},
      {"QSG_RHI_BACKEND", qEnvironmentVariable("QSG_RHI_BACKEND")}};
    return {{"timesMs", times}, {"window", windowInfo}, {"item", itemInfo}, {"gl", gl},
      {"qtVersion", QString::fromLatin1(qVersion())}, {"platformPlugin", QGuiApplication::platformName()},
      {"requestedGraphicsApi", static_cast<int>(QQuickWindow::graphicsApi())}, {"graphicsEnvironment", environment},
      {"sceneGraphFailed", m_failed.load()}, {"sceneGraphErrorCode", m_errorCode.load()}, {"sceneGraphError", m_error},
      {"desktop", windowsDesktopMetadata()}};
  }

  const std::chrono::steady_clock::time_point m_start;
  std::atomic<qint64> m_visible{-1}, m_expose{-1}, m_update{-1}, m_sceneGraph{-1}, m_sync{-1}, m_render{-1};
  std::atomic<qint64> m_swap{-1}, m_readyEmitted{-1}, m_readyDelivered{-1};
  std::atomic<qint64> m_errorTime{-1};
  std::atomic<bool> m_failed{false}, m_glCaptured{false};
  QMutex m_mutex;
  bool m_glValid = false;
  bool m_glPresent = false;
  std::atomic<int> m_errorCode{-1};
  QString m_error, m_glFormat, m_vendor, m_renderer, m_version;
};

#endif
