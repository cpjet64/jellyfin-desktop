#include "system/RecoveryBridgeSession.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QThread>
#include <QWebChannel>
#include <QWebChannelAbstractTransport>
#include <QtTest/QtTest>
#include <thread>

class RecoveryTransport : public QWebChannelAbstractTransport
{
  Q_OBJECT
public:
  void sendMessage(const QJsonObject& message) override { replies.append(message); }
  void receive(const QJsonObject& message) { emit messageReceived(message, this); }
  QList<QJsonObject> replies;
};

static QVariantMap request(const QString& session, const QString& id)
{
  return {{"version", 1}, {"sessionId", session}, {"commandId", id}, {"operation", "handshake"}};
}

class TestRecoveryBridgeSession : public QObject
{
  Q_OBJECT
private slots:
  void isolatedSessionsAndMalformedRequests();
  void queuedOldChannelCannotAffectNewSession();
  void windowDestructionRevokes();
  void channelDestructionDestroysEndpoint();
  void endpointDestructionDoesNotRemint();
  void wrongThreadConstructionIsRevoked();
  void quitRevokes();
};

void TestRecoveryBridgeSession::isolatedSessionsAndMalformedRequests()
{
  QObject windowA, windowB;
  QWebChannel channelA, channelB;
  RecoveryBridgeSession a(&windowA, &channelA), b(&windowB, &channelB);
  auto* first = a.endpoint();
  auto* second = b.endpoint();
  QVERIFY(first && second && first != second);
  QVERIFY(!first->sessionId().isEmpty() && first->sessionId() != second->sessionId());
  const auto original = first->sessionId();
  QVERIFY(!first->setProperty("sessionId", "forged"));
  QCOMPARE(first->sessionId(), original);
  QCOMPARE(first->invoke(request(first->sessionId(), "1")).value("status").toString(), "accepted");
  QCOMPARE(first->invoke(request(first->sessionId(), "1")).value("status").toString(), "duplicate");
  QCOMPARE(second->invoke(request(first->sessionId(), "2")).value("status").toString(), "rejected");
  auto extra = request(second->sessionId(), "1"); extra.insert("token", "never-echo-this");
  const auto rejected = second->invoke(extra);
  QCOMPARE(rejected.value("status").toString(), "rejected");
  QVERIFY(!QString::fromUtf8(QJsonDocument::fromVariant(rejected).toJson()).contains("never-echo-this"));
  for (const auto& id : {QString(), QStringLiteral("01"), QStringLiteral("-1"), QStringLiteral("9007199254740992")})
    QCOMPARE(second->invoke(request(second->sessionId(), id)).value("status").toString(), "rejected");
  QCOMPARE(second->invoke(request(second->sessionId(), "1")).value("status").toString(), "accepted");
  auto conflict = request(second->sessionId(), "2"); conflict["operation"] = "play";
  QCOMPARE(second->invoke(conflict).value("status").toString(), "rejected");
  QCOMPARE(second->invoke(request(second->sessionId(), "2")).value("status").toString(), "accepted");
  QCOMPARE(second->invoke(request(second->sessionId(), "1")).value("status").toString(), "rejected");
  a.revoke(); a.revoke();
  QCOMPARE(first->invoke(request(first->sessionId(), "2")).value("status").toString(), "revoked");
  QCOMPARE(second->invoke(request(second->sessionId(), "3")).value("status").toString(), "accepted");
}

void TestRecoveryBridgeSession::wrongThreadConstructionIsRevoked()
{
  QObject window;
  QWebChannel channel;
  bool rejected = false;
  auto* thread = QThread::create([&] {
    RecoveryBridgeSession session(&window, &channel);
    rejected = session.endpoint()->invoke(request(session.endpoint()->sessionId(), "1"))
                 .value("status").toString() == "revoked";
  });
  thread->start();
  if (!thread->wait(3000)) qFatal("session test thread did not settle");
  delete thread;
  QVERIFY(rejected);
}

void TestRecoveryBridgeSession::channelDestructionDestroysEndpoint()
{
  QObject window;
  auto* channel = new QWebChannel;
  auto* session = new RecoveryBridgeSession(&window, channel);
  QPointer<RecoveryBridgeEndpoint> endpoint = session->endpoint();
  delete channel;
  QVERIFY(endpoint.isNull());
}

void TestRecoveryBridgeSession::queuedOldChannelCannotAffectNewSession()
{
  QObject oldWindow, newWindow;
  QWebChannel oldChannel, newChannel;
  RecoveryBridgeSession oldSession(&oldWindow, &oldChannel), newSession(&newWindow, &newChannel);
  RecoveryTransport transport;
  oldChannel.registerObject("recovery", oldSession.endpoint());
  oldChannel.connectTo(&transport);
  transport.receive({{"type", 3}, {"id", 1}});
  const int method = oldSession.endpoint()->metaObject()->indexOfMethod("invoke(QVariantMap)");
  QVERIFY(method >= 0);
  const auto payload = QJsonObject::fromVariantMap(request(oldSession.endpoint()->sessionId(), "1"));
  transport.receive({{"type", 6}, {"object", "recovery"}, {"method", method},
                     {"args", QJsonArray{payload}}, {"id", 40}});
  bool sawAccepted = false;
  for (const auto& reply : transport.replies)
    if (reply.value("id").toInt() == 40) sawAccepted = reply.value("data").toObject().value("status").toString() == "accepted";
  QVERIFY(sawAccepted);
  const auto next = QJsonObject::fromVariantMap(request(oldSession.endpoint()->sessionId(), "2"));
  const QJsonObject call{{"type", 6}, {"object", "recovery"}, {"method", method},
                         {"args", QJsonArray{next}}, {"id", 42}};
  std::thread sender([&] { transport.receive(call); });
  sender.join();
  oldSession.revoke();
  QCoreApplication::processEvents();
  bool sawRevoked = false;
  for (const auto& reply : transport.replies)
    if (reply.value("id").toInt() == 42) sawRevoked = reply.value("data").toObject().value("status").toString() == "revoked";
  QVERIFY(sawRevoked);
  QCOMPARE(oldSession.endpoint()->invoke(request(oldSession.endpoint()->sessionId(), "2")).value("status").toString(), "revoked");
  QCOMPARE(newSession.endpoint()->invoke(request(newSession.endpoint()->sessionId(), "1")).value("status").toString(), "accepted");
  oldChannel.disconnectFrom(&transport);
}

void TestRecoveryBridgeSession::endpointDestructionDoesNotRemint()
{
  QObject window;
  QWebChannel channel;
  RecoveryBridgeSession session(&window, &channel);
  delete session.endpoint();
  session.revoke();
  QVERIFY(session.endpoint() == nullptr);
}

void TestRecoveryBridgeSession::windowDestructionRevokes()
{
  auto* window = new QObject;
  QWebChannel channel;
  RecoveryBridgeSession session(window, &channel);
  auto* endpoint = session.endpoint();
  const auto id = endpoint->sessionId();
  delete window;
  QCOMPARE(endpoint->invoke(request(id, "1")).value("status").toString(), "revoked");
}

void TestRecoveryBridgeSession::quitRevokes()
{
  QObject window;
  QWebChannel channel;
  RecoveryBridgeSession session(&window, &channel);
  const auto id = session.endpoint()->sessionId();
  QVERIFY(QMetaObject::invokeMethod(QCoreApplication::instance(), "aboutToQuit", Qt::DirectConnection));
  QCOMPARE(session.endpoint()->invoke(request(id, "1")).value("status").toString(), "revoked");
}

QTEST_GUILESS_MAIN(TestRecoveryBridgeSession)
#include "test_recovery_bridge_session.moc"
