#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include "RecoveryInitialOpening.h"

class QWebChannel;

// This endpoint proves only possession of one host-created view/channel object.
// It does not identify a document, authorize playback, or enable recovery.
class RecoveryBridgeEndpoint final : public QObject
{
  Q_OBJECT
  Q_PROPERTY(QString sessionId READ sessionId CONSTANT)
  Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged)
public:
  QString sessionId() const { return m_sessionId; }
  bool isReady() const { return m_ready && !m_revoked; }
  Q_INVOKABLE QVariantMap invoke(const QVariantMap& request);

signals:
  void revoked();
  void readyChanged(bool ready);

private:
  friend class RecoveryBridgeSession;
  RecoveryBridgeEndpoint(QObject* parent, QObject* view, QObject* channel);
  void revoke();
  void markReady();
  const QString m_sessionId;
  quint64 m_lastCommand = 0;
  bool m_revoked = false;
  bool m_ready = false;
  QPointer<QObject> m_view, m_channel;
};

// Host-only owner: never export this object or create it from a web call.
class RecoveryBridgeSession final : public QObject
{
  Q_OBJECT
public:
  RecoveryBridgeSession(QObject* window, QObject* view, QWebChannel* channel);
  ~RecoveryBridgeSession() override;
  RecoveryBridgeEndpoint* endpoint() const { return m_endpoint.data(); }
  void revoke();
  bool openInitial(const QUrl& url, const std::function<bool()>& dispatch);
  void observeRequest(const QUrl& url, bool mainFrame, bool expectedKind);
  void observeStarted(const QUrl& url);
  QObject* view() const { return m_view.data(); }

private:
  QPointer<RecoveryBridgeEndpoint> m_endpoint;
  QPointer<QObject> m_view;
  RecoveryInitialOpening m_opening;
  void synchronize();
};
