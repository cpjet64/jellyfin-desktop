#include "system/RecoveryBridgeSession.h"
#include "system/WebClientFaultGuard.h"
#include "utils/Utils.h"
#include <QWebChannel>
#include <QThread>
#include <QtTest/QtTest>

class RecoveryFakeWindow : public QObject
{
  Q_OBJECT
public:
  int dispatches = 0;
  std::function<void()> onDispatch;
  Q_INVOKABLE void dispatchInitialClient(const QVariant&) { ++dispatches; if (onDispatch) onDispatch(); }
};

class TestRecoveryBridgeHost : public QObject
{
  Q_OBJECT
private slots:
  void channelSwapIsRevoked();
  void startupFallbackIsOneShot();
  void registeredOpeningIsOneShot();
  void exitingPreventsStartup();
  void missingChannelIsFatal();
  void attachNavigateAndRendererLoss();
};

static QVariantMap handshake(RecoveryBridgeEndpoint* endpoint, const QString& command)
{
  return {{"version", 1}, {"sessionId", endpoint->sessionId()},
          {"commandId", command}, {"operation", "handshake"}};
}

void TestRecoveryBridgeHost::missingChannelIsFatal()
{
  RecoveryFakeWindow window;
  window.setProperty("webUrl", QStringLiteral("https://example.invalid/start"));
  QVERIFY_EXCEPTION_THROWN(WebClientFaultGuard::registerChannelForWindow(&window), FatalException);
  QCOMPARE(window.dispatches, 0);
}

void TestRecoveryBridgeHost::registeredOpeningIsOneShot()
{
  auto& guard = WebClientFaultGuard::instance();
  const QUrl url(QStringLiteral("https://example.invalid/start"));
  for (bool requestFirst : {false, true}) {
    RecoveryFakeWindow window;
    QObject view(&window);
    QWebChannel channel;
    view.setObjectName("web");
    view.setProperty("webChannel", QVariant::fromValue<QObject*>(&channel));
    window.setProperty("webChannel", QVariant::fromValue<QObject*>(&channel));
    window.setProperty("webUrl", url.toString());
    window.setProperty("recoveryNavigationObserved", false);
    WebClientFaultGuard::registerChannelForWindow(&window);
    auto* endpoint = qobject_cast<RecoveryBridgeEndpoint*>(channel.registeredObjects().value("recoveryBridge"));
    QVERIFY(endpoint);
    const auto identity = endpoint->sessionId();
    bool registeredBeforeDispatch = false;
    window.onDispatch = [&] {
      registeredBeforeDispatch = channel.registeredObjects().value("recoveryBridge") == endpoint;
      if (requestFirst) guard.observeRecoveryRequest(&window, &view, url, true, true);
      else guard.observeRecoveryStarted(&window, &view, url);
    };
    WebClientFaultGuard::openInitialClient(&window);
    QVERIFY(registeredBeforeDispatch);
    QCOMPARE(window.dispatches, 1);
    QVERIFY(!endpoint->isReady());
    if (requestFirst) guard.observeRecoveryStarted(&window, &view, url);
    else guard.observeRecoveryRequest(&window, &view, url, true, true);
    QVERIFY(endpoint->isReady());
    WebClientFaultGuard::openInitialClient(&window);
    QCOMPARE(window.dispatches, 1);
    QCOMPARE(endpoint->sessionId(), identity);
    QCOMPARE(endpoint->invoke(handshake(endpoint, "1")).value("status").toString(), "revoked");
  }
}

void TestRecoveryBridgeHost::exitingPreventsStartup()
{
  auto& guard = WebClientFaultGuard::instance();
  RecoveryFakeWindow window;
  window.setProperty("webUrl", QStringLiteral("https://example.invalid/start"));
  QVERIFY(QMetaObject::invokeMethod(QCoreApplication::instance(), "aboutToQuit", Qt::DirectConnection));
  WebClientFaultGuard::openInitialClient(&window);
  QCOMPARE(window.dispatches, 0);
  guard.m_recoveryExiting = false; // Isolate the following host tests in this test process.
}

void TestRecoveryBridgeHost::startupFallbackIsOneShot()
{
  auto& guard = WebClientFaultGuard::instance();
  RecoveryFakeWindow window;
  QObject view(&window);
  QWebChannel channel;
  view.setObjectName("web");
  view.setProperty("webChannel", QVariant::fromValue<QObject*>(&channel));
  window.setProperty("webChannel", QVariant::fromValue<QObject*>(&channel));
  window.setProperty("webUrl", QStringLiteral("https://example.invalid/start"));
  window.setProperty("recoveryNavigationObserved", true);
  WebClientFaultGuard::registerChannelForWindow(&window);
  auto session = guard.m_recoverySessions.value(&window);
  QVERIFY(session);
  QVERIFY(!channel.registeredObjects().contains("recoveryBridge"));
  WebClientFaultGuard::openInitialClient(&window);
  QCOMPARE(window.dispatches, 1);
  WebClientFaultGuard::openInitialClient(&window);
  QCOMPARE(window.dispatches, 1);
  QCOMPARE(session->endpoint()->invoke(handshake(session->endpoint(), "1")).value("status").toString(), "revoked");
}

void TestRecoveryBridgeHost::channelSwapIsRevoked()
{
  auto& guard = WebClientFaultGuard::instance();
  QObject window, view(&window);
  QWebChannel original, replacement;
  window.setProperty("recoveryNavigationObserved", false);
  view.setObjectName("web");
  view.setProperty("webChannel", QVariant::fromValue<QObject*>(&original));
  guard.attachRecoveryBridge(&window, &original);
  auto session = guard.m_recoverySessions.value(&window);
  QVERIFY(session);
  const QUrl url(QStringLiteral("https://example.invalid/start"));
  QVERIFY(session->openInitial(url, [] { return true; }));
  guard.observeRecoveryStarted(&window, &view, url);
  view.setProperty("webChannel", QVariant::fromValue<QObject*>(&replacement));
  guard.observeRecoveryRequest(&window, &view, url, true, true);
  QCOMPARE(session->endpoint()->invoke(handshake(session->endpoint(), "1")).value("status").toString(), "revoked");
}

void TestRecoveryBridgeHost::attachNavigateAndRendererLoss()
{
  auto& guard = WebClientFaultGuard::instance();
  QObject firstWindow, secondWindow, observedWindow, movedWindow;
  QWebChannel firstChannel, secondChannel, observedChannel, movedChannel, replacementChannel;
  QObject firstView(&firstWindow), secondView(&secondWindow), observedView(&observedWindow), movedView(&movedWindow);
  for (auto* view : {&firstView, &secondView, &observedView, &movedView}) view->setObjectName("web");
  firstView.setProperty("webChannel", QVariant::fromValue<QObject*>(&firstChannel));
  secondView.setProperty("webChannel", QVariant::fromValue<QObject*>(&secondChannel));
  observedView.setProperty("webChannel", QVariant::fromValue<QObject*>(&observedChannel));
  movedView.setProperty("webChannel", QVariant::fromValue<QObject*>(&movedChannel));
  firstWindow.setProperty("recoveryNavigationObserved", false);
  secondWindow.setProperty("recoveryNavigationObserved", false);
  observedWindow.setProperty("recoveryNavigationObserved", true);
  movedWindow.setProperty("recoveryNavigationObserved", false);
  auto* worker = QThread::create([&] { guard.attachRecoveryBridge(&firstWindow, &firstChannel); });
  worker->start();
  if (!worker->wait(3000)) qFatal("host test thread did not settle");
  delete worker;
  QVERIFY(!firstChannel.registeredObjects().contains("recoveryBridge"));
  guard.attachRecoveryBridge(&firstWindow, &firstChannel);
  guard.attachRecoveryBridge(&secondWindow, &secondChannel);
  guard.attachRecoveryBridge(&observedWindow, &observedChannel);
  guard.attachRecoveryBridge(&movedWindow, &movedChannel);
  auto* first = qobject_cast<RecoveryBridgeEndpoint*>(firstChannel.registeredObjects().value("recoveryBridge"));
  auto* second = qobject_cast<RecoveryBridgeEndpoint*>(secondChannel.registeredObjects().value("recoveryBridge"));
  auto* moved = qobject_cast<RecoveryBridgeEndpoint*>(movedChannel.registeredObjects().value("recoveryBridge"));
  QVERIFY(first && second && moved && first != second);
  const QUrl url(QStringLiteral("https://example.invalid/start"));
  for (auto* window : {&firstWindow, &secondWindow, &movedWindow}) {
    auto session = guard.m_recoverySessions.value(window);
    QVERIFY(session && session->openInitial(url, [] { return true; }));
    session->observeRequest(url, true, true);
    session->observeStarted(url);
  }
  QVERIFY(!observedChannel.registeredObjects().contains("recoveryBridge"));
  QCOMPARE(first->invoke(handshake(first, "1")).value("status").toString(), "accepted");
  QCOMPARE(second->invoke(handshake(second, "1")).value("status").toString(), "accepted");
  guard.attachRecoveryBridge(&firstWindow, &firstChannel);
  QCOMPARE(first->invoke(handshake(first, "2")).value("status").toString(), "accepted");
  guard.attachRecoveryBridge(&movedWindow, &replacementChannel);
  QCOMPARE(moved->invoke(handshake(moved, "1")).value("status").toString(), "revoked");
  QVERIFY(!replacementChannel.registeredObjects().contains("recoveryBridge"));
  guard.observeRecoveryNavigation(&firstWindow);
  QCOMPARE(first->invoke(handshake(first, "3")).value("status").toString(), "revoked");
  guard.attachRecoveryBridge(&firstWindow, &firstChannel);
  QCOMPARE(firstChannel.registeredObjects().value("recoveryBridge"), static_cast<QObject*>(first));
  QCOMPARE(second->invoke(handshake(second, "2")).value("status").toString(), "accepted");
  guard.observeRendererLoss(0, 0);
  QCOMPARE(second->invoke(handshake(second, "3")).value("status").toString(), "revoked");
  RecoveryFakeWindow afterLoss;
  afterLoss.setProperty("webUrl", QStringLiteral("https://example.invalid/start"));
  WebClientFaultGuard::openInitialClient(&afterLoss);
  QCOMPARE(afterLoss.dispatches, 0);
  guard.attachRecoveryBridge(&observedWindow, &observedChannel);
  QVERIFY(!observedChannel.registeredObjects().contains("recoveryBridge"));
}

QTEST_GUILESS_MAIN(TestRecoveryBridgeHost)
#include "test_recovery_bridge_host.moc"
