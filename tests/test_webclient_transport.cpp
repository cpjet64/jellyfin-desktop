#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonObject>
#include <QThread>
#include <QWebChannel>
#include <QWebChannelAbstractTransport>
#include <atomic>
#include <thread>

class TestTransport : public QWebChannelAbstractTransport
{
  Q_OBJECT
public:
  void sendMessage(const QJsonObject&) override {}
  void receive(const QJsonObject& message) { emit messageReceived(message, this); }
};

class ExportedFixture : public QObject
{
  Q_OBJECT
  Q_PROPERTY(int value READ value WRITE setValue NOTIFY valueChanged)
public:
  int value() const { return m_value; }
  void setValue(int value) { m_value = value; }
  Q_INVOKABLE void action() { ++calls; actionThread = QThread::currentThread(); }
  int calls = 0;
  QThread* actionThread = nullptr;
signals:
  void valueChanged();
private:
  int m_value = 0;
};

class TestWebClientTransport : public QObject
{
  Q_OBJECT
private slots:
  void cutoff();
};

void TestWebClientTransport::cutoff()
{
  QWebChannel channel;
  TestTransport transport;
  ExportedFixture input;
  ExportedFixture settings;
  channel.registerObject("input", &input);
  channel.registerObject("settings", &settings);
  channel.connectTo(&transport);
  transport.receive({{"type", 3}, {"id", 1}});
  const int method = input.metaObject()->indexOfMethod("action()");
  const int property = settings.metaObject()->indexOfProperty("value");
  QVERIFY(method >= 0);
  QVERIFY(property >= 0);
  const QJsonObject action{{"type", 6}, {"object", "input"}, {"method", method},
                           {"args", QJsonArray{}}, {"id", 2}};
  const QJsonObject setValue{{"type", 9}, {"object", "settings"}, {"property", property}, {"value", 42}};
  transport.receive(action);
  transport.receive({{"type", 9}, {"object", "settings"}, {"property", property}, {"value", 7}});
  QCOMPARE(input.calls, 1);
  QCOMPARE(settings.value(), 7);
  QCOMPARE(input.actionThread, QThread::currentThread());
  std::atomic<int> emitted{0};
  QObject::connect(&transport, &QWebChannelAbstractTransport::messageReceived, &transport,
                   [&emitted](const QJsonObject&, QWebChannelAbstractTransport*) { ++emitted; }, Qt::DirectConnection);
  // Emit from a worker while the receiver's GUI thread is not pumping events.
  // The signal deliveries are queued before the transport is disconnected.
  std::thread worker([&] { transport.receive(action); transport.receive(setValue); });
  worker.join();
  QCOMPARE(emitted.load(), 2);
  channel.disconnectFrom(&transport);
  QCoreApplication::processEvents();
  QCOMPARE(input.calls, 1);
  QCOMPARE(settings.value(), 7);
}

QTEST_GUILESS_MAIN(TestWebClientTransport)
#include "test_webclient_transport.moc"
