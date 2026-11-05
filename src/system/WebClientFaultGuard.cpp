#include "WebClientFaultGuard.h"
#include "RecoveryBridgeSession.h"

#include <QDebug>
#include <QCoreApplication>
#include <QThread>
#include <QWebChannel>

#include "core/ComponentManager.h"
#include "core/Globals.h"
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

void WebClientFaultGuard::attachRecoveryBridge(QObject* window, QWebChannel* channel)
{
  if (!window || !channel) return;
  if (QThread::currentThread() != thread() || window->thread() != thread() || channel->thread() != thread()) return;
  if (m_recoverySessions.contains(window)) {
    const auto old = m_recoverySessions.value(window);
    if (old && old->parent() != channel) old->revoke();
    return;
  }
  auto* session = new RecoveryBridgeSession(window, channel);
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
