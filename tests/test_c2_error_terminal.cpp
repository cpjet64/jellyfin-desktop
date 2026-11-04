#include <QtTest>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include "c2_error_terminal.h"
#include "c2_error_verdict.h"

class TestC2ErrorTerminal : public QObject
{
  Q_OBJECT
private slots:
  void evidence();
  void properties();
  void verdictSentinel();
  void verdictSubprocess();
};

void TestC2ErrorTerminal::evidence()
{
  const std::vector<C2TerminalEvent> valid{{MPV_EVENT_START_FILE, 42},
    {MPV_EVENT_FILE_LOADED}, {MPV_EVENT_END_FILE, 42, MPV_END_FILE_REASON_ERROR, -1}};
  QVERIFY(c2ErrorCleanupEligible(true, true, valid, 42));
  QVERIFY(!c2ErrorCleanupEligible(false, true, valid, 42));
  QVERIFY(!c2ErrorCleanupEligible(true, false, valid, 42));
  QVERIFY(!c2ErrorEntry(valid, 0));
  QVERIFY(!c2ErrorEntry(valid, 43));
  auto changed = valid;
  changed.erase(changed.begin() + 1);
  QVERIFY(!c2ErrorEntry(changed, 42));
  changed = valid;
  changed.back().reason = MPV_END_FILE_REASON_STOP;
  QVERIFY(!c2ErrorEntry(changed, 42));
  changed = valid;
  changed.back().entry = 43;
  QVERIFY(!c2ErrorEntry(changed, 42));
  changed = valid;
  changed.push_back({MPV_EVENT_START_FILE, 42});
  QVERIFY(!c2ErrorEntry(changed, 42));
  changed = valid;
  changed.push_back({MPV_EVENT_QUEUE_OVERFLOW});
  QVERIFY(!c2ErrorEntry(changed, 42));
  changed = valid;
  changed.back().insertCount = 1;
  QVERIFY(!c2ErrorEntry(changed, 42));
  changed = valid;
  changed.back().error = 0;
  QVERIFY(!c2ErrorEntry(changed, 42));
}

void TestC2ErrorTerminal::properties()
{
  QVERIFY(c2ErrorTerminalProperties(0, 1, MPV_ERROR_PROPERTY_UNAVAILABLE, 0, true));
  QVERIFY(!c2ErrorTerminalProperties(-1, 1, MPV_ERROR_PROPERTY_UNAVAILABLE, 0, true));
  QVERIFY(!c2ErrorTerminalProperties(0, 0, MPV_ERROR_PROPERTY_UNAVAILABLE, 0, true));
  QVERIFY(!c2ErrorTerminalProperties(0, 1, 0, 0, true));
  QVERIFY(!c2ErrorTerminalProperties(0, 1, MPV_ERROR_PROPERTY_UNAVAILABLE, -1, true));
  QVERIFY(!c2ErrorTerminalProperties(0, 1, MPV_ERROR_PROPERTY_UNAVAILABLE, 0, false));
}

void TestC2ErrorTerminal::verdictSentinel()
{
  const QString marker = qEnvironmentVariable("C2_ERROR_SENTINEL");
  if (marker.isEmpty()) return;
  const std::vector<C2TerminalEvent> events{{MPV_EVENT_START_FILE, 42}, {MPV_EVENT_FILE_LOADED},
    {MPV_EVENT_END_FILE, 42, MPV_END_FILE_REASON_ERROR, -1}};
  const int64_t entry = qEnvironmentVariableIsSet("C2_ERROR_INVALID") ? 43 : 42;
  if (!c2ErrorCleanupEligible(true, true, events, entry)) return;
  std::atomic<bool> fatal{true};
  finishErrorCase(fatal, [&] {
    if (!fatal.load()) return false;
    QFile file(marker);
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write("cleaned") == 7;
  });
  if (fatal.load()) {
    QFile file(marker + ".latched");
    if (file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) file.write("latched");
  }
}

void TestC2ErrorTerminal::verdictSubprocess()
{
  QTemporaryDir directory(QDir::currentPath() + "/c2-error-verdict-XXXXXX");
  QVERIFY(directory.isValid());
  for (bool invalid : {false, true}) {
    const QString marker = directory.path() + (invalid ? "/invalid" : "/valid");
    QProcess child;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("C2_ERROR_SENTINEL", marker);
    if (invalid) env.insert("C2_ERROR_INVALID", "1");
    else env.remove("C2_ERROR_INVALID");
    child.setProcessEnvironment(env);
    const QString report = marker + ".txt";
    child.start(QCoreApplication::applicationFilePath(), {"verdictSentinel", "-o", report + ",txt"});
    QVERIFY(child.waitForStarted(5000));
    QVERIFY(child.waitForFinished(10000));
    QFile reportFile(report);
    QVERIFY(reportFile.open(QIODevice::ReadOnly));
    const QByteArray output = reportFile.readAll();
    QCOMPARE(child.exitStatus(), QProcess::NormalExit);
    QCOMPARE(QFile::exists(marker), !invalid);
    QCOMPARE(QFile::exists(marker + ".latched"), !invalid);
    if (invalid) QCOMPARE(child.exitCode(), 0);
    else {
      QVERIFY(child.exitCode() != 0);
      QVERIFY(output.contains("FAIL!"));
      QVERIFY(output.contains("1 failed"));
      QVERIFY(output.contains("Finished testing"));
    }
  }
}

QTEST_GUILESS_MAIN(TestC2ErrorTerminal)
#include "test_c2_error_terminal.moc"
