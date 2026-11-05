#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantMap>

class QWebChannel;

// This endpoint proves only possession of one host-created view/channel object.
// It does not identify a document, authorize playback, or enable recovery.
class RecoveryBridgeEndpoint final : public QObject
{
  Q_OBJECT
  Q_PROPERTY(QString sessionId READ sessionId CONSTANT)
public:
  QString sessionId() const { return m_sessionId; }
  Q_INVOKABLE QVariantMap invoke(const QVariantMap& request);

signals:
  void revoked();

private:
  friend class RecoveryBridgeSession;
  explicit RecoveryBridgeEndpoint(QObject* parent);
  void revoke();
  const QString m_sessionId;
  quint64 m_lastCommand = 0;
  bool m_revoked = false;
};

// Host-only owner: never export this object or create it from a web call.
class RecoveryBridgeSession final : public QObject
{
  Q_OBJECT
public:
  RecoveryBridgeSession(QObject* window, QWebChannel* channel);
  ~RecoveryBridgeSession() override;
  RecoveryBridgeEndpoint* endpoint() const { return m_endpoint.data(); }
  void revoke();

private:
  QPointer<RecoveryBridgeEndpoint> m_endpoint;
};
