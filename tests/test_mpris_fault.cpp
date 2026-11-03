#include <QtTest/QtTest>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>

#include "core/ProfileManager.h"
#include "input/InputComponent.h"
#include "mpris/MprisComponent.h"
#include "mpris/MprisPlayerAdaptor.h"
#include "shared/Paths.h"
#include "system/SystemComponent.h"
#include "system/WebClientFaultGuard.h"

static const char* objectPath = "/org/mpris/MediaPlayer2";
static const char* playerInterface = "org.mpris.MediaPlayer2.Player";

class PropertyObserver : public QObject
{
  Q_OBJECT
public:
  int falseSeekUpdates = 0;
public slots:
  void changed(const QString& interface, const QVariantMap& changes, const QStringList&)
  {
    if (interface == playerInterface && changes.contains("CanSeek") &&
        !changes.value("CanSeek").toBool()) ++falseSeekUpdates;
  }
};

static QDBusMessage invoke(const QString& service, const QString& method,
                           const QVariantList& arguments = {})
{
  auto message = QDBusMessage::createMethodCall(service, objectPath, playerInterface, method);
  message.setArguments(arguments);
  QDBusPendingCallWatcher watcher(QDBusConnection::sessionBus().asyncCall(message, 5000));
  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
  QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  timer.start(6000);
  if (!watcher.isFinished()) loop.exec();
  return watcher.reply();
}

class TestMprisFault : public QObject
{
  Q_OBJECT
private slots:
  void faultRejectsSeek();
};

void TestMprisFault::faultRejectsSeek()
{
  QVERIFY2(QDBusConnection::sessionBus().isConnected(), "A private session bus is required");
  QTemporaryDir temp(QDir::currentPath() + "/mpris-fault-XXXXXX");
  QVERIFY(temp.isValid());
  Paths::setConfigDir(temp.path() + "/config");
  Paths::setCacheDir(temp.path() + "/cache");
  const QString profile = QFileInfo(ProfileManager::activeProfile().cacheDir()).fileName();
  const QString service = "org.mpris.MediaPlayer2.JellyfinDesktop.profile_" + profile;
  MprisComponent mpris;
  QVERIFY(mpris.componentInitialize());
  auto* adaptor = mpris.findChild<MprisPlayerAdaptor*>();
  QVERIFY(adaptor);
  QSignalSpy seeked(adaptor, &MprisPlayerAdaptor::Seeked);
  QSignalSpy positions(&InputComponent::Get(), &InputComponent::positionSeek);
  QSignalSpy stopResults(&InputComponent::Get(), &InputComponent::nativeStopResult);
  QSignalSpy forwarded(&InputComponent::Get(), &InputComponent::hostInput);
  PropertyObserver observer;
  QVERIFY(QDBusConnection::sessionBus().connect(service, objectPath,
      "org.freedesktop.DBus.Properties", "PropertiesChanged", &observer,
      SLOT(changed(QString,QVariantMap,QStringList))));

  mpris.notifyDurationChange(120000);
  mpris.notifyPlaybackState("Playing");
  mpris.notifyMetadata({{"Name", "Fixture"}, {"MediaType", "Video"}});
  const auto track = qvariant_cast<QDBusObjectPath>(mpris.metadata().value("mpris:trackid"));
  QVERIFY(mpris.canSeek());
  QVERIFY(mpris.canControl());
  SystemComponent::Get().hello("fixture");
  QCOMPARE(invoke(service, "Stop").type(), QDBusMessage::ReplyMessage);
  QCOMPARE(forwarded.count(), 1);
  QCOMPARE(stopResults.count(), 0);
  QCOMPARE(invoke(service, "SetPosition", {QVariant::fromValue(QDBusObjectPath("/wrong")), 5000000LL}).type(), QDBusMessage::ReplyMessage);
  QCOMPARE(positions.count(), 0);
  QCOMPARE(invoke(service, "SetPosition", {QVariant::fromValue(track), 5000000LL}).type(), QDBusMessage::ReplyMessage);
  QCOMPARE(positions.count(), 1);
  QVERIFY(QMetaObject::invokeMethod(&mpris, "onPlayerPositionUpdate", Q_ARG(quint64, 5000)));
  QCOMPARE(seeked.count(), 1);
  QCOMPARE(invoke(service, "Seek", {1000000LL}).type(), QDBusMessage::ReplyMessage);
  QCOMPARE(positions.count(), 2);

  observer.falseSeekUpdates = 0;
  WebClientFaultGuard::instance().observeRendererLoss(0, 17);
  QVERIFY(!mpris.canSeek());
  QVERIFY(mpris.canControl());
  QTRY_VERIFY_WITH_TIMEOUT(observer.falseSeekUpdates > 0, 5000);
  QCOMPARE(invoke(service, "Seek", {1000000LL}).type(), QDBusMessage::ReplyMessage);
  QCOMPARE(invoke(service, "SetPosition", {QVariant::fromValue(track), 6000000LL}).type(), QDBusMessage::ReplyMessage);
  QCOMPARE(positions.count(), 2);
  QVERIFY(QMetaObject::invokeMethod(&mpris, "onPlayerPositionUpdate", Q_ARG(quint64, 6000)));
  QCOMPARE(seeked.count(), 1);
  QCOMPARE(invoke(service, "Stop").type(), QDBusMessage::ReplyMessage);
  QCOMPARE(stopResults.count(), 1);
  QCOMPARE(forwarded.count(), 1);
}

QTEST_MAIN(TestMprisFault)
#include "test_mpris_fault.moc"
