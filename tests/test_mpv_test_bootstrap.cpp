#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include "mpv_test_bootstrap.h"

using namespace MpvTestBootstrap;

class TestMpvTestBootstrap : public QObject
{
  Q_OBJECT
private slots:
  void absentAndExisting();
  void rejectLinksAndErrorsBeforeInitialization();
  void rootAndDescendantCases();
  void pathSyntax();
  void unusedWindowsDrive();
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

void TestMpvTestBootstrap::rootAndDescendantCases()
{
  const QString owned = QDir::currentPath() + "/owned";
  const QString candidate = owned + "/mpvqt/mpvqt.conf";
#ifdef Q_OS_WIN
  const QString anchor = candidate.left(3);
#else
  const QString anchor = "/";
#endif
  for (const Entry failure : {Entry::Missing, Entry::Error, Entry::Link, Entry::File}) {
    QStringList inspected;
    int initializations = 0;
    QString error;
    const auto inspect = [&](const QString& path) {
      inspected.append(path);
      return path == anchor ? failure : Entry::Directory;
    };
    if (checkCandidate(candidate, owned, inspect, &error)) ++initializations;
    QCOMPARE(initializations, 0);
    QCOMPARE(inspected, QStringList{anchor});
    QVERIFY(!error.isEmpty());
  }
  const auto check = [&](const QString& target, Entry status) {
    QString error;
    return checkCandidate(target, owned, [&](const QString& path) {
      return path == target ? status : Entry::Directory;
    }, &error);
  };
  QVERIFY(check(candidate, Entry::Missing));
  QVERIFY(check(candidate, Entry::File));
  QVERIFY(!check(candidate, Entry::Directory));
  QVERIFY(!check(QDir::currentPath() + "/foreign/mpvqt.conf", Entry::File));
  for (const Entry failure : {Entry::Link, Entry::Error, Entry::File}) {
    QString error;
    const auto inspect = [&](const QString& path) {
      return path == owned + "/mpvqt" ? failure : Entry::Directory;
    };
    QVERIFY(!checkCandidate(candidate, owned, inspect, &error));
  }
  QString error;
  const auto missingDirectory = [&](const QString& path) {
    return path == owned + "/mpvqt" ? Entry::Missing : Entry::Directory;
  };
  QVERIFY(checkCandidate(candidate, owned, missingDirectory, &error));
}

void TestMpvTestBootstrap::pathSyntax()
{
  int inspected = 0;
  QString error;
  const auto inspect = [&](const QString&) { ++inspected; return Entry::Missing; };
#ifdef Q_OS_WIN
  const QStringList rejected = {"C:relative/file", "/current/drive", "\\\\server\\share\\file",
                                "\\\\?\\C:\\file", "\\\\.\\C:\\file", "relative/file"};
  for (const QString& path : rejected) {
    QVERIFY(!checkCandidate(path, "C:/owned", inspect, &error));
    QVERIFY(!checkCandidate("C:/owned/file", path, inspect, &error));
    QVERIFY2(inspected == 0, qPrintable(path));
  }
  QCOMPARE(inspected, 0);
  const auto safe = [](const QString& path) { return path == "C:/" ? Entry::Directory : Entry::Missing; };
  QVERIFY(checkCandidate("C:/folder with spaces/mpvqt.conf", "C:/owned", safe, &error));
#else
  QVERIFY(!checkCandidate("relative/file", "/owned", inspect, &error));
  QCOMPARE(inspected, 0);
  const auto safe = [](const QString& path) { return path == "/" ? Entry::Directory : Entry::Missing; };
  QVERIFY(checkCandidate("/folder with spaces/mpvqt.conf", "/owned", safe, &error));
#endif
}

void TestMpvTestBootstrap::unusedWindowsDrive()
{
#ifdef Q_OS_WIN
  const DWORD drives = GetLogicalDrives();
  QVERIFY2(drives != 0, "GetLogicalDrives failed; native unused-drive proof is unavailable");
  int index = -1;
  for (int i = 25; i >= 0; --i) if (!(drives & (DWORD(1) << i))) { index = i; break; }
  QVERIFY2(index >= 0, "No unused drive letter; native unused-drive proof is unavailable");
  const DWORD rechecked = GetLogicalDrives();
  QVERIFY2(rechecked != 0 && !(rechecked & (DWORD(1) << index)), "Drive availability changed; native proof unavailable");
  const QString candidate = QString(QChar('A' + index)) + ":/mpv-test-unassigned/mpvqt.conf";
  QString error;
  QVERIFY(!checkCandidateOnDisk(candidate, QDir::currentPath(), &error));
  QVERIFY(!error.isEmpty());
#else
  QVERIFY(QDir::isAbsolutePath("/"));
#endif
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
