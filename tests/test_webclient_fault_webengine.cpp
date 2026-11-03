#include <QtTest/QtTest>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QThread>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include "system/WebClientFaultGuard.h"

class WebFixtureInput : public QObject
{
  Q_OBJECT
public:
  Q_INVOKABLE void action() { ++calls; callThread = QThread::currentThread(); }
  int calls = 0;
  QThread* callThread = nullptr;
};

class TestWebClientFaultWebEngine : public QObject
{
  Q_OBJECT
private slots:
  void channelCannotReattach();
};

void TestWebClientFaultWebEngine::channelCannotReattach()
{
  auto& guard = WebClientFaultGuard::instance();
  WebFixtureInput input;
  QQuickView view;
  view.rootContext()->setContextProperty("webClientFaultGuard", &guard);
  view.rootContext()->setContextProperty("fixtureInput", &input);
  view.setSource(QUrl::fromLocalFile(QStringLiteral(TEST_SOURCE_DIR "/webclient_fault_fixture.qml")));
  QCOMPARE(view.status(), QQuickView::Ready);
  QObject* root = view.rootObject();
  QVERIFY(root);
  WebClientFaultGuard::registerChannelForWindow(root);
  view.show();
  QVERIFY(QMetaObject::invokeMethod(root, "loadPage"));
  QTRY_COMPARE_WITH_TIMEOUT(root->property("loads").toInt(), 1, 15000);
  QVERIFY(!guard.rendererLost());
  QVERIFY(QMetaObject::invokeMethod(root, "reloadPage"));
  QTRY_COMPARE_WITH_TIMEOUT(root->property("loads").toInt(), 2, 15000);
  QVERIFY(!guard.rendererLost());
  QVERIFY(QMetaObject::invokeMethod(root, "connectBridge"));
  QTRY_VERIFY_WITH_TIMEOUT((QMetaObject::invokeMethod(root, "checkReady"),
                            root->property("bridgeReady").toBool()), 15000);
  QVariant transports;
  QVERIFY(QMetaObject::invokeMethod(root, "transportCount", Q_RETURN_ARG(QVariant, transports)));
  QCOMPARE(transports.toInt(), 1);
  QVERIFY(QMetaObject::invokeMethod(root, "evaluate", Q_ARG(QVariant, QVariant(QStringLiteral("window.fixtureInput.action(); 'sent'")))));
  QTRY_COMPARE_WITH_TIMEOUT(input.calls, 1, 5000);
  QCOMPARE(input.callThread, QThread::currentThread());
  QVERIFY(QMetaObject::invokeMethod(root, "evaluate", Q_ARG(QVariant, QVariant(QStringLiteral("typeof webClientFaultGuard")))));
  QTRY_COMPARE_WITH_TIMEOUT(root->property("result").toString(), QStringLiteral("undefined"), 5000);
  QVERIFY(QMetaObject::invokeMethod(root, "loseRenderer"));
  QVERIFY(guard.rendererLost());
  QVERIFY(QMetaObject::invokeMethod(root, "transportCount", Q_RETURN_ARG(QVariant, transports)));
  QCOMPARE(transports.toInt(), 0);
  QVERIFY(QMetaObject::invokeMethod(root, "evaluate", Q_ARG(QVariant, QVariant(QStringLiteral("try { window.fixtureInput.action(); } catch (e) {} 'done'")))));
  QTRY_COMPARE_WITH_TIMEOUT(root->property("result").toString(), QStringLiteral("done"), 5000);
  QTest::qWait(250);
  QCOMPARE(input.calls, 1);
  QVERIFY(QMetaObject::invokeMethod(root, "recreate"));
  QVERIFY(QMetaObject::invokeMethod(root, "loadPage"));
  QTRY_COMPARE_WITH_TIMEOUT(root->property("loads").toInt(), 3, 15000);
  QVERIFY(root->property("webChannel").value<QObject*>() == nullptr);
  WebClientFaultGuard::registerChannelForWindow(root);
  QVERIFY(QMetaObject::invokeMethod(root, "transportCount", Q_RETURN_ARG(QVariant, transports)));
  QCOMPARE(transports.toInt(), 0);
}

int main(int argc, char** argv)
{
  QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
  QtWebEngineQuick::initialize();
  QGuiApplication app(argc, argv);
  TestWebClientFaultWebEngine test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_webclient_fault_webengine.moc"
