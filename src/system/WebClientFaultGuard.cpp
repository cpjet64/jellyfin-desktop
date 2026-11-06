#include "WebClientFaultGuard.h"
#include "RecoveryBridgeSession.h"

#include <QDebug>
#include <QCoreApplication>
#include <QThread>
#include <QWebChannel>

#include "core/ComponentManager.h"
#include "core/Globals.h"
#include "settings/SettingsComponent.h"
#include "utils/Utils.h"

WebClientFaultGuard::WebClientFaultGuard()
{
  if (QCoreApplication::instance()) connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
    m_recoveryExiting = true;
    for (const auto& session : m_recoverySessions) if (session) session->revoke();
  });
}

WebClientFaultGuard& WebClientFaultGuard::instance()
{
  static WebClientFaultGuard guard;
  return guard;
}

void WebClientFaultGuard::installContextProperties()
{
  Globals::SetContextProperty("components", &ComponentManager::Get().getQmlPropertyMap());
  Globals::SetContextProperty("webClientFaultGuard", &instance());
}

void WebClientFaultGuard::registerChannelForWindow(QObject* window)
{
  if (!window || QThread::currentThread() != instance().thread() || window->thread() != instance().thread())
    throw FatalException(QObject::tr("Web channel window is unavailable"));
  if (instance().rendererLost()) return;
  auto* object = qvariant_cast<QObject*>(window->property("webChannel"));
  auto* channel = qobject_cast<QWebChannel*>(object);
  if (!channel) {
    instance().observeRecoveryNavigation(window);
    throw FatalException(QObject::tr("Web channel is unavailable"));
  }
  instance().attachRecoveryBridge(window, channel);
  ComponentManager::Get().setWebChannel(channel);
}

void WebClientFaultGuard::openInitialClient(QObject* window)
{
  if (!window || QThread::currentThread() != instance().thread() || window->thread() != instance().thread()) return;
  if (instance().m_recoveryExiting || instance().rendererLost()) return;
  if (instance().m_initialDispatches.contains(window)) {
    instance().observeRecoveryNavigation(window);
    return;
  }
  instance().m_initialDispatches.insert(window);
  QObject::connect(window, &QObject::destroyed, &instance(), [window] { instance().m_initialDispatches.remove(window); });
  if (window->property("recoveryNavigationObserved").toBool()) {
    instance().observeRecoveryNavigation(window);
  }
  auto* view = window ? window->findChild<QObject*>(QStringLiteral("web")) : nullptr;
  const QString overrideUrl = window->property("webUrl").toString();
  const QUrl url(overrideUrl.isEmpty() ? SettingsComponent::Get().getWebClientUrl(window->property("webDesktopMode").toBool()) : overrideUrl);
  bool attempted = false;
  bool delivered = false;
  const auto dispatch = [window, url, &attempted, &delivered] {
    attempted = true;
    delivered = QMetaObject::invokeMethod(window, "dispatchInitialClient", Qt::DirectConnection,
                                           Q_ARG(QVariant, QVariant(url)));
    return delivered;
  };
  const auto session = instance().m_recoverySessions.value(window);
  if (session && view && session->view() == view && qvariant_cast<QObject*>(view->property("webChannel")) == session->parent()) {
    session->openInitial(url, dispatch);
  } else if (session) {
    session->revoke();
  }
  if (!attempted) dispatch(); // Ordinary startup remains available without a qualified session.
  if (!delivered) throw FatalException(QObject::tr("Initial web client dispatch failed"));
}

void WebClientFaultGuard::attachRecoveryBridge(QObject* window, QWebChannel* channel)
{
  if (!window || !channel) return;
  if (QThread::currentThread() != thread() || window->thread() != thread() || channel->thread() != thread()) return;
  if (m_recoverySessions.contains(window)) {
    const auto old = m_recoverySessions.value(window);
    if (old && old->parent() != channel) old->revoke();
    return;
  }
  auto* view = window->findChild<QObject*>(QStringLiteral("web"));
  if (!view || view->thread() != thread() || qvariant_cast<QObject*>(view->property("webChannel")) != channel) return;
  auto* session = new RecoveryBridgeSession(window, view, channel);
  m_recoverySessions.insert(window, session);
  connect(window, &QObject::destroyed, this, [this, window] { m_recoverySessions.remove(window); });
  const auto observed = window->property("recoveryNavigationObserved");
  if (!observed.isValid() || observed.metaType().id() != QMetaType::Bool || observed.toBool() ||
      rendererLost() || m_recoveryExiting || channel->registeredObjects().contains(QStringLiteral("recoveryBridge"))) {
    session->revoke();
    return;
  }
  channel->registerObject(QStringLiteral("recoveryBridge"), session->endpoint());
}

void WebClientFaultGuard::observeRecoveryRequest(QObject* window, QObject* view, const QUrl& url, bool mainFrame, bool expectedKind)
{
  if (QThread::currentThread() != thread()) return;
  if (auto session = m_recoverySessions.value(window)) {
    if (!view || session->view() != view || qvariant_cast<QObject*>(view->property("webChannel")) != session->parent()) session->revoke();
    else session->observeRequest(url, mainFrame, expectedKind);
  }
}

void WebClientFaultGuard::observeRecoveryStarted(QObject* window, QObject* view, const QUrl& url)
{
  if (QThread::currentThread() != thread()) return;
  if (auto session = m_recoverySessions.value(window)) {
    if (!view || session->view() != view || qvariant_cast<QObject*>(view->property("webChannel")) != session->parent()) session->revoke();
    else session->observeStarted(url);
  }
}

void WebClientFaultGuard::observeRecoveryNavigation(QObject* window)
{
  if (QThread::currentThread() != thread()) return;
  if (auto session = m_recoverySessions.value(window)) session->revoke();
}

void WebClientFaultGuard::observeRendererLoss(int status, int exitCode)
{
  Q_ASSERT(QThread::currentThread() == thread());
  if (m_rendererLost) return;
  m_rendererLost = true;
  m_status = status;
  m_exitCode = exitCode;
  for (const auto& session : m_recoverySessions) if (session) session->revoke();
  qWarning() << "Web renderer terminated, status" << status << "exit code" << exitCode;
  emit rendererLostChanged();
}
