#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <iostream>
#include <sstream>
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
  void failedStartupKeepsPreviousSink();
  void reentrantDiagnostic();
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

// The rotation failure writes to stderr while holding the production lock.
// Emit a Qt diagnostic from that write to exercise same-thread reentry.
class DiagnosticStream : public std::stringbuf
{
protected:
  std::streamsize xsputn(const char* text, std::streamsize count) override
  {
    if (!triggered)
    {
      triggered = true;
      qWarning("nested api_key=0123456789abcdef0123456789abcdef");
    }
    return std::stringbuf::xsputn(text, count);
  }
private:
  bool triggered = false;
};

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

void TestLogRotation::failedStartupKeepsPreviousSink()
{
  QTemporaryDir dir(QDir::currentPath() + "/log-rotation-XXXXXX");
  QVERIFY(dir.isValid());
  useTestDirectory(dir.path() + "/blocked");
  const QString blockedLogDir = ProfileManager::activeProfile().logDir();
  QVERIFY(QDir().mkpath(QFileInfo(blockedLogDir).absolutePath()));
  QFile obstruction(blockedLogDir);
  QVERIFY(obstruction.open(QIODevice::WriteOnly));
  obstruction.close();
  Log::Init();
  qWarning("terminal logging after failed startup");

  useTestDirectory(dir.path() + "/working");
  const QString workingLogDir = ProfileManager::activeProfile().logDir();
  QVERIFY(QDir().mkpath(workingLogDir));
  Log::Init();
  qWarning("before failed restart");
  useTestDirectory(dir.path() + "/blocked");
  Log::Init();
  qWarning("after failed restart");
  Log::Cleanup();
  const auto files = QDir(workingLogDir).entryList({"jellyfin-desktop-*.log"}, QDir::Files);
  QCOMPARE(files.size(), 1);
  const QByteArray contents = readFile(workingLogDir + "/" + files.first());
  QVERIFY(contents.contains("before failed restart"));
  QVERIFY(contents.contains("after failed restart"));
}

void TestLogRotation::reentrantDiagnostic()
{
  if (!qEnvironmentVariableIsSet("JELLYFIN_LOG_REENTRY_CHILD"))
  {
    QTemporaryDir results(QDir::currentPath() + "/log-reentry-result-XXXXXX");
    QVERIFY(results.isValid());
    const QString resultPath = results.path() + "/result.txt";
    QProcess child;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("JELLYFIN_LOG_REENTRY_CHILD", "1");
    child.setProcessEnvironment(env);
    child.start(QCoreApplication::applicationFilePath(),
                {"reentrantDiagnostic", "-o", resultPath + ",txt"});
    QVERIFY(child.waitForStarted(5000));
    if (!child.waitForFinished(10000))
    {
      child.kill();
      child.waitForFinished();
      QFAIL("Logging diagnostic deadlocked");
    }
    QCOMPARE(child.exitStatus(), QProcess::NormalExit);
    QVERIFY2(child.exitCode() == 0, readFile(resultPath).constData());
    return;
  }

  QTemporaryDir dir(QDir::currentPath() + "/log-reentry-XXXXXX");
  QVERIFY(dir.isValid());
  useTestDirectory(dir.path());
  const QString logDir = ProfileManager::activeProfile().logDir();
  QVERIFY(QDir().mkpath(logDir));
  QVERIFY(QDir().mkpath(mainLogPath()));
  Log::Init();
  DiagnosticStream capture;
  std::streambuf* original = std::cerr.rdbuf(&capture);
  Log::RotateLog();
  std::cerr.rdbuf(original);
  const QByteArray output = QByteArray::fromStdString(capture.str());
  QVERIFY(output.contains("nested api_key=xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"));
  QVERIFY(!output.contains("0123456789abcdef0123456789abcdef"));
  qInfo("logging continues after nested warning");
  Log::Cleanup();
  const auto files = QDir(logDir).entryList({"jellyfin-desktop-*.log"}, QDir::Files);
  QCOMPARE(files.size(), 1);
  QVERIFY(readFile(logDir + "/" + files.first()).contains("logging continues after nested warning"));
}

QTEST_GUILESS_MAIN(TestLogRotation)
#include "test_log_rotation.moc"
