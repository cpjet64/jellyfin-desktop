#include "WebClientFaultGuard.h"

#include <QDebug>
#include <QThread>

#include "core/ComponentManager.h"
#include "core/Globals.h"

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
