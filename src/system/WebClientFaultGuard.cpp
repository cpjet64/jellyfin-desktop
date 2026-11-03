#include "WebClientFaultGuard.h"

#include <QDebug>
#include <QThread>
#include <QWebChannel>

#include "core/ComponentManager.h"
#include "core/Globals.h"
#include "utils/Utils.h"

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
  if (!channel) throw FatalException(QObject::tr("Web channel is unavailable"));
  ComponentManager::Get().setWebChannel(channel);
}

void WebClientFaultGuard::observeRendererLoss(int status, int exitCode)
{
  Q_ASSERT(QThread::currentThread() == thread());
  if (m_rendererLost) return;
  m_rendererLost = true;
  m_status = status;
  m_exitCode = exitCode;
  qWarning() << "Web renderer terminated, status" << status << "exit code" << exitCode;
  emit rendererLostChanged();
}
