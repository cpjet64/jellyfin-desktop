#pragma once

#include <QObject>

// This object is only exposed to the native QML engine, never to QWebChannel.
class WebClientFaultGuard final : public QObject
{
  Q_OBJECT
  Q_PROPERTY(bool rendererLost READ rendererLost NOTIFY rendererLostChanged)
public:
  static WebClientFaultGuard& instance();
  static void installContextProperties();
  static void registerChannelForWindow(QObject* window);
  bool rendererLost() const { return m_rendererLost; }
  int terminationStatus() const { return m_status; }
  int terminationExitCode() const { return m_exitCode; }
  Q_INVOKABLE void observeRendererLoss(int status, int exitCode);

signals:
  void rendererLostChanged();

private:
  WebClientFaultGuard() = default;
  bool m_rendererLost = false;
  int m_status = 0;
  int m_exitCode = 0;
};
