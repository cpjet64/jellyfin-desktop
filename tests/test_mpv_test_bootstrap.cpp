#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include "mpv_test_bootstrap.h"

using namespace MpvTestBootstrap;

class TestMpvTestBootstrap : public QObject
{
  Q_OBJECT
private slots:
  void absentAndExisting();
  void rejectLinksAndErrorsBeforeInitialization();
  void runtimePrerequisites();
};

void TestMpvTestBootstrap::absentAndExisting()
{
  QTemporaryDir owned(QDir::currentPath() + "/mpv-guard-owned-XXXXXX");
  QTemporaryDir foreign(QDir::currentPath() + "/mpv-guard-foreign-XXXXXX");
  QVERIFY(owned.isValid());
  QVERIFY(foreign.isValid());
  const QString safe = owned.path() + "/mpvqt/mpvqt.conf";
  const QString other = foreign.path() + "/mpvqt/mpvqt.conf";
  QString error;
  QVERIFY(checkCandidateOnDisk(safe, owned.path(), &error));
  QVERIFY(QDir().mkpath(owned.path() + "/mpvqt"));
  QFile local(safe);
  QVERIFY(local.open(QIODevice::WriteOnly));
  local.close();
  QVERIFY(checkCandidateOnDisk(safe, owned.path(), &error));
  QVERIFY(QDir().mkpath(foreign.path() + "/mpvqt"));
  QFile external(other);
  QVERIFY(external.open(QIODevice::WriteOnly));
  external.close();
  QVERIFY(!checkCandidateOnDisk(other, owned.path(), &error));
}

void TestMpvTestBootstrap::rejectLinksAndErrorsBeforeInitialization()
{
  const QString root = QDir::currentPath();
  const QString candidate = root + "/fixture/mpvqt/mpvqt.conf";
  for (const Entry failure : {Entry::Link, Entry::Error}) {
    for (const QString& failedPath : {candidate, root + "/fixture/mpvqt"}) {
      int initializationCount = 0;
      QString error;
      const auto inspect = [&](const QString& path) {
        return path == failedPath ? failure : Entry::Directory;
      };
      if (checkCandidate(candidate, root, inspect, &error)) ++initializationCount;
      QCOMPARE(initializationCount, 0);
      QVERIFY(!error.isEmpty());
    }
  }
}

void TestMpvTestBootstrap::runtimePrerequisites()
{
  QVERIFY(QCoreApplication::instance());
  QTemporaryDir owned(QDir::currentPath() + "/mpv-guard-runtime-XXXXXX");
  QVERIFY(owned.isValid());
#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
  if (!qEnvironmentVariableIsSet("JELLYFIN_TEST_OWNED_ROOT")) {
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(owned.path() + "/xdg-config"));
    qputenv("XDG_CACHE_HOME", QFile::encodeName(owned.path() + "/xdg-cache"));
  }
#endif
  const QString root = qEnvironmentVariable("JELLYFIN_TEST_OWNED_ROOT", owned.path());
  QString error;
  QVERIFY2(prepareMpvTestEnvironment(root, &error), qPrintable(error));
}

QTEST_GUILESS_MAIN(TestMpvTestBootstrap)
#include "test_mpv_test_bootstrap.moc"
