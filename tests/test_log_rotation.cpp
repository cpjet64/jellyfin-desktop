#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <thread>
#include <vector>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "core/ProfileManager.h"
#include "shared/Names.h"
#include "shared/Paths.h"
#include "utils/Log.h"

class TestLogRotation : public QObject
{
  Q_OBJECT
private slots:
  void initTestCase();
  void cleanup();
  void cleanupTestCase();
  void normalRotation();
  void failedRotationKeepsWriting();
  void repeatedStartupAndConcurrentMessages();
  void rotateAfterCleanup();
  void lockedArchiveKeepsDestination();
private:
  QtMessageHandler previousHandler = nullptr;
};

void TestLogRotation::initTestCase()
{
  previousHandler = qInstallMessageHandler(nullptr);
  qInstallMessageHandler(previousHandler);
}

void TestLogRotation::cleanup()
{
  Log::Cleanup();
  Paths::setConfigDir(QString());
  Paths::setCacheDir(QString());
}

void TestLogRotation::cleanupTestCase()
{
  qInstallMessageHandler(previousHandler);
}

static void useTestDirectory(const QString& path)
{
  Paths::setConfigDir(path + "/data");
  Paths::setCacheDir(path + "/cache");
}

static QString mainLogPath()
{
  return ProfileManager::activeProfile().logDir() + "/" + Names::DataName() + ".log";
}

static QByteArray readFile(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  return file.readAll();
}

void TestLogRotation::normalRotation()
{
  QTemporaryDir dir(QDir::currentPath() + "/log-rotation-XXXXXX");
  QVERIFY(dir.isValid());
  useTestDirectory(dir.path());
  QVERIFY(QDir().mkpath(ProfileManager::activeProfile().logDir()));
  QFile previous(mainLogPath());
  QVERIFY(previous.open(QIODevice::WriteOnly));
  previous.write("previous\n");
  previous.close();
  Log::Init();
  qInfo("before rotation");
  Log::RotateLog();
  qInfo("after rotation");
  Log::Cleanup();
  QVERIFY(readFile(mainLogPath()).contains("before rotation"));
  QVERIFY(readFile(mainLogPath()).contains("after rotation"));
  QCOMPARE(readFile(mainLogPath() + ".1"), QByteArray("previous\n"));
}

void TestLogRotation::failedRotationKeepsWriting()
{
  QTemporaryDir dir(QDir::currentPath() + "/log-rotation-XXXXXX");
  QVERIFY(dir.isValid());
  useTestDirectory(dir.path());
  const QString logDir = ProfileManager::activeProfile().logDir();
  QVERIFY(QDir().mkpath(logDir));
  QVERIFY(QDir().mkpath(mainLogPath())); // A directory cannot be rotated as a file.
  Log::Init();
  qInfo("before failed rotation");
  Log::RotateLog();
  qInfo("after failed rotation");
  Log::Cleanup();
  const auto files = QDir(logDir).entryList({"jellyfin-desktop-*.log"}, QDir::Files);
  QCOMPARE(files.size(), 1);
  const QByteArray contents = readFile(logDir + "/" + files.first());
  QVERIFY(contents.contains("before failed rotation"));
  QVERIFY(contents.contains("after failed rotation"));
}

void TestLogRotation::repeatedStartupAndConcurrentMessages()
{
  QTemporaryDir dir(QDir::currentPath() + "/log-rotation-XXXXXX");
  QVERIFY(dir.isValid());
  useTestDirectory(dir.path());
  QVERIFY(QDir().mkpath(ProfileManager::activeProfile().logDir()));
  Log::Init();
  qInfo("first startup");
  Log::Init();
  std::vector<std::thread> workers;
  for (int i = 0; i < 4; ++i)
    workers.emplace_back([] { for (int n = 0; n < 100; ++n) qInfo("concurrent marker"); });
  Log::RotateLog();
  for (auto& worker : workers)
    worker.join();
  Log::Cleanup();
  QCOMPARE(readFile(mainLogPath()).count("concurrent marker"), 400);
  const auto files = QDir(ProfileManager::activeProfile().logDir())
                         .entryList({"jellyfin-desktop-*.log"}, QDir::Files);
  QCOMPARE(files.size(), 1);
  QVERIFY(readFile(ProfileManager::activeProfile().logDir() + "/" + files.first()).contains("first startup"));
}

void TestLogRotation::rotateAfterCleanup()
{
  QTemporaryDir dir(QDir::currentPath() + "/log-rotation-XXXXXX");
  QVERIFY(dir.isValid());
  useTestDirectory(dir.path());
  QVERIFY(QDir().mkpath(ProfileManager::activeProfile().logDir()));
  Log::Init();
  Log::Cleanup();
  Log::RotateLog(); // The test runner's timeout bounds this deadlock regression.
  QVERIFY(true);
}

void TestLogRotation::lockedArchiveKeepsDestination()
{
#ifdef Q_OS_WIN
  QTemporaryDir dir(QDir::currentPath() + "/log-rotation-XXXXXX");
  QVERIFY(dir.isValid());
  useTestDirectory(dir.path());
  QVERIFY(QDir().mkpath(ProfileManager::activeProfile().logDir()));
  const QString source = mainLogPath() + ".9";
  const QString destination = mainLogPath() + ".10";
  QFile sourceFile(source);
  QVERIFY(sourceFile.open(QIODevice::WriteOnly));
  QCOMPARE(sourceFile.write("source archive"), qint64(14));
  sourceFile.close();
  QFile destinationFile(destination);
  QVERIFY(destinationFile.open(QIODevice::WriteOnly));
  QCOMPARE(destinationFile.write("older archive"), qint64(13));
  destinationFile.close();
  Log::Init();
  HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(source.utf16()), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  QVERIFY(handle != INVALID_HANDLE_VALUE);
  Log::RotateLog();
  CloseHandle(handle);
  QCOMPARE(readFile(source), QByteArray("source archive"));
  QCOMPARE(readFile(destination), QByteArray("older archive"));
  qInfo("still logging after locked archive");
  Log::Cleanup();
  const auto files = QDir(ProfileManager::activeProfile().logDir())
                         .entryList({"jellyfin-desktop-*.log"}, QDir::Files);
  QCOMPARE(files.size(), 1);
  QVERIFY(readFile(ProfileManager::activeProfile().logDir() + "/" + files.first())
              .contains("still logging after locked archive"));
#else
  QSKIP("Windows sharing semantics required");
#endif
}

QTEST_GUILESS_MAIN(TestLogRotation)
#include "test_log_rotation.moc"
