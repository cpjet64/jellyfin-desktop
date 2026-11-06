#include "system/RecoveryBridgeSession.h"
#include <QCoreApplication>
#include <QEvent>
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

static void prepare(RecoveryBridgeSession& session)
{
  session.view()->setProperty("webChannel", QVariant::fromValue<QObject*>(session.parent()));
  const QUrl url(QStringLiteral("https://example.invalid/start"));
  QVERIFY(session.openInitial(url, [] { return true; }));
  session.observeRequest(url, true, true);
  session.observeStarted(url);
  QVERIFY(session.endpoint()->isReady());
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
  void initialOpeningOrders();
  void unexpectedOpeningEventsRevoke();
  void pendingWireRevocation();
  void channelSwapRejectsCall();
  void dispatchFailuresAndReentry();
  void hideIsNotExit();
  void delayedSameUrlIsOnlyProvisional();
};

void TestRecoveryBridgeSession::delayedSameUrlIsOnlyProvisional()
{
  QObject window, view;
  QWebChannel channel;
  view.setProperty("webChannel", QVariant::fromValue<QObject*>(&channel));
  RecoveryBridgeSession session(&window, &view, &channel);
  const QUrl url(QStringLiteral("https://example.invalid/start"));
  // These callbacks model observations queued before the host dispatch.
  auto delayedRequest = [&] { session.observeRequest(url, true, true); };
  auto delayedStart = [&] { session.observeStarted(url); };
  QVERIFY(session.openInitial(url, [] { return true; }));
  delayedRequest(); delayedStart();
  QVERIFY(session.endpoint()->isReady());
  const auto response = session.endpoint()->invoke(request(session.endpoint()->sessionId(), "1"));
  QCOMPARE(response.value("status").toString(), "accepted");
  QCOMPARE(response.size(), 3);
  QVERIFY(!response.contains("capability") && !response.contains("candidate"));
}

void TestRecoveryBridgeSession::hideIsNotExit()
{
  QObject window, view;
  QWebChannel channel;
  RecoveryBridgeSession session(&window, &view, &channel);
  prepare(session);
  QEvent hide(QEvent::Hide);
  QCoreApplication::sendEvent(&window, &hide);
  auto* endpoint = session.endpoint();
  QCOMPARE(endpoint->invoke(request(endpoint->sessionId(), "1")).value("status").toString(), "accepted");
  QVERIFY(QMetaObject::invokeMethod(QCoreApplication::instance(), "aboutToQuit", Qt::DirectConnection));
  QCOMPARE(endpoint->invoke(request(endpoint->sessionId(), "2")).value("status").toString(), "revoked");
}

void TestRecoveryBridgeSession::dispatchFailuresAndReentry()
{
  const QUrl url(QStringLiteral("https://example.invalid/start"));
  QObject window, view;
  QWebChannel channel;
  view.setProperty("webChannel", QVariant::fromValue<QObject*>(&channel));
  RecoveryBridgeSession failed(&window, &view, &channel);
  int dispatches = 0;
  QVERIFY(!failed.openInitial(url, [&] { ++dispatches; return false; }));
  QVERIFY(!failed.openInitial(url, [&] { ++dispatches; return true; }));
  QCOMPARE(dispatches, 1);
  QCOMPARE(failed.endpoint()->invoke(request(failed.endpoint()->sessionId(), "1")).value("status").toString(), "revoked");
  RecoveryBridgeSession failedAfterSignals(&window, &view, &channel);
  int readySignals = 0;
  connect(failedAfterSignals.endpoint(), &RecoveryBridgeEndpoint::readyChanged, this, [&](bool) { ++readySignals; });
  QVERIFY(!failedAfterSignals.openInitial(url, [&] {
    failedAfterSignals.observeRequest(url, true, true);
    failedAfterSignals.observeStarted(url);
    return false;
  }));
  QCOMPARE(readySignals, 0);
  QCOMPARE(failedAfterSignals.endpoint()->invoke(request(failedAfterSignals.endpoint()->sessionId(), "1")).value("status").toString(), "revoked");
  RecoveryBridgeSession thrown(&window, &view, &channel);
  QVERIFY_EXCEPTION_THROWN(thrown.openInitial(url, []() -> bool { throw 1; }), int);
  QCOMPARE(thrown.endpoint()->invoke(request(thrown.endpoint()->sessionId(), "1")).value("status").toString(), "revoked");
  RecoveryBridgeSession reentrant(&window, &view, &channel);
  bool readyDuringDispatch = true;
  QVERIFY(reentrant.openInitial(url, [&] {
    reentrant.observeRequest(url, true, true);
    reentrant.observeStarted(url);
    readyDuringDispatch = reentrant.endpoint()->isReady();
    return true;
  }));
  QVERIFY(!readyDuringDispatch);
  QVERIFY(reentrant.endpoint()->isReady());
}

void TestRecoveryBridgeSession::channelSwapRejectsCall()
{
  QObject window, view;
  QWebChannel original, replacement;
  RecoveryBridgeSession session(&window, &view, &original);
  prepare(session);
  view.setProperty("webChannel", QVariant::fromValue<QObject*>(&replacement));
  auto* endpoint = session.endpoint();
  QCOMPARE(endpoint->invoke(request(endpoint->sessionId(), "1")).value("status").toString(), "revoked");
  view.setProperty("webChannel", QVariant::fromValue<QObject*>(&original));
  QCOMPARE(endpoint->invoke(request(endpoint->sessionId(), "1")).value("status").toString(), "revoked");
}

void TestRecoveryBridgeSession::initialOpeningOrders()
{
  const QUrl url(QStringLiteral("https://example.invalid/start"));
  for (bool requestFirst : {false, true}) {
    QObject window, view;
    QWebChannel channel;
    RecoveryBridgeSession session(&window, &view, &channel);
    view.setProperty("webChannel", QVariant::fromValue<QObject*>(&channel));
    auto* endpoint = session.endpoint();
    int dispatches = 0, notifications = 0;
    connect(endpoint, &RecoveryBridgeEndpoint::readyChanged, this, [&](bool ready) { if (ready) ++notifications; });
    QCOMPARE(endpoint->invoke(request(endpoint->sessionId(), "1")).value("status").toString(), "pending");
    QVERIFY(session.openInitial(url, [&] { ++dispatches; return true; }));
    if (requestFirst) { session.observeRequest(url, true, true); session.observeStarted(url); }
    else { session.observeStarted(url); session.observeRequest(url, true, true); }
    QCOMPARE(dispatches, 1);
    QCOMPARE(notifications, 1);
    QCOMPARE(endpoint->invoke(request(endpoint->sessionId(), "1")).value("status").toString(), "accepted");
    QVERIFY(!session.openInitial(url, [&] { ++dispatches; return true; }));
    QCOMPARE(dispatches, 1);
    QCOMPARE(endpoint->invoke(request(endpoint->sessionId(), "2")).value("status").toString(), "revoked");
  }
}

void TestRecoveryBridgeSession::unexpectedOpeningEventsRevoke()
{
  const QUrl url(QStringLiteral("https://example.invalid/start"));
  for (int scenario = 0; scenario < 5; ++scenario) {
    QObject window, view;
    QWebChannel channel;
    RecoveryBridgeSession session(&window, &view, &channel);
    auto* endpoint = session.endpoint();
    int dispatches = 0;
    if (scenario == 0) session.observeRequest(url, true, true);
    else QVERIFY(session.openInitial(url, [&] { ++dispatches; return true; }));
    if (scenario == 1) session.observeRequest(url, true, false);
    if (scenario == 2) { session.observeRequest(url, true, true); session.observeRequest(url, true, true); }
    if (scenario == 3) { session.observeStarted(url); session.observeStarted(url); }
    if (scenario == 4) session.observeRequest(QUrl(QStringLiteral("https://example.invalid/other")), true, true);
    session.observeStarted(url);
    QCOMPARE(endpoint->invoke(request(endpoint->sessionId(), "1")).value("status").toString(), "revoked");
    QCOMPARE(dispatches, scenario == 0 ? 0 : 1);
  }
}

void TestRecoveryBridgeSession::pendingWireRevocation()
{
  QObject window, view;
  QWebChannel channel;
  RecoveryBridgeSession session(&window, &view, &channel);
  RecoveryTransport transport;
  channel.registerObject("recovery", session.endpoint());
  channel.connectTo(&transport);
  transport.receive({{"type", 3}, {"id", 1}});
  const int method = session.endpoint()->metaObject()->indexOfMethod("invoke(QVariantMap)");
  const auto payload = QJsonObject::fromVariantMap(request(session.endpoint()->sessionId(), "1"));
  const QJsonObject call{{"type", 6}, {"object", "recovery"}, {"method", method}, {"args", QJsonArray{payload}}, {"id", 43}};
  std::thread sender([&] { transport.receive(call); });
  sender.join();
  session.revoke();
  QCoreApplication::processEvents();
  bool revoked = false;
  for (const auto& reply : transport.replies)
    if (reply.value("id").toInt() == 43) revoked = reply.value("data").toObject().value("status").toString() == "revoked";
  QVERIFY(revoked);
  channel.disconnectFrom(&transport);
}

void TestRecoveryBridgeSession::isolatedSessionsAndMalformedRequests()
{
  QObject windowA, windowB;
  QWebChannel channelA, channelB;
  RecoveryBridgeSession a(&windowA, &windowA, &channelA), b(&windowB, &windowB, &channelB);
  prepare(a); prepare(b);
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
    RecoveryBridgeSession session(&window, &window, &channel);
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
  auto* session = new RecoveryBridgeSession(&window, &window, channel);
  QPointer<RecoveryBridgeEndpoint> endpoint = session->endpoint();
  delete channel;
  QVERIFY(endpoint.isNull());
}

void TestRecoveryBridgeSession::queuedOldChannelCannotAffectNewSession()
{
  QObject oldWindow, newWindow;
  QWebChannel oldChannel, newChannel;
  RecoveryBridgeSession oldSession(&oldWindow, &oldWindow, &oldChannel), newSession(&newWindow, &newWindow, &newChannel);
  prepare(oldSession); prepare(newSession);
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
  RecoveryBridgeSession session(&window, &window, &channel);
  delete session.endpoint();
  session.revoke();
  QVERIFY(session.endpoint() == nullptr);
}

void TestRecoveryBridgeSession::windowDestructionRevokes()
{
  auto* window = new QObject;
  QWebChannel channel;
  RecoveryBridgeSession session(window, window, &channel);
  auto* endpoint = session.endpoint();
  const auto id = endpoint->sessionId();
  delete window;
  QCOMPARE(endpoint->invoke(request(id, "1")).value("status").toString(), "revoked");
}

void TestRecoveryBridgeSession::quitRevokes()
{
  QObject window;
  QWebChannel channel;
  RecoveryBridgeSession session(&window, &window, &channel);
  const auto id = session.endpoint()->sessionId();
  QVERIFY(QMetaObject::invokeMethod(QCoreApplication::instance(), "aboutToQuit", Qt::DirectConnection));
  QCOMPARE(session.endpoint()->invoke(request(id, "1")).value("status").toString(), "revoked");
}

QTEST_GUILESS_MAIN(TestRecoveryBridgeSession)
#include "test_recovery_bridge_session.moc"
