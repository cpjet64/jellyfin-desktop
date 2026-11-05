#include <QtTest/QtTest>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include "synchronous_stop_trace.h"

class TestSynchronousStopTrace : public QObject
{
  Q_OBJECT
private slots:
  void failureStages();
  void newOnlyPreservesExisting();
  void partialFileIsPreserved();
};

void TestSynchronousStopTrace::failureStages()
{
  const QByteArray data("receipt");
  for (int failure = 0; failure < 3; ++failure) {
    int writes = 0, flushes = 0;
    const bool ok = persistSynchronousTrace(data, [&] { return failure != 0; },
      [&](const QByteArray& value) -> qint64 { ++writes; return failure == 1 ? value.size() - 1 : value.size(); },
      [&] { ++flushes; return failure != 2; });
    QVERIFY(!ok);
    QCOMPARE(writes, failure == 0 ? 0 : 1);
    QCOMPARE(flushes, failure == 2 ? 1 : 0);
  }
  QVERIFY(persistSynchronousTrace(data, [] { return true; },
          [](const QByteArray& value) -> qint64 { return value.size(); }, [] { return true; }));
}

void TestSynchronousStopTrace::newOnlyPreservesExisting()
{
  QTemporaryDir dir(QDir::currentPath() + "/synchronous-trace-XXXXXX");
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("receipt.json");
  QVERIFY(persistSynchronousTraceFile(path, "first"));
  QVERIFY(!persistSynchronousTraceFile(path, "second"));
  QFile file(path);
  QVERIFY(file.open(QIODevice::ReadOnly));
  QCOMPARE(file.readAll(), QByteArray("first"));
}

void TestSynchronousStopTrace::partialFileIsPreserved()
{
  QTemporaryDir dir(QDir::currentPath() + "/synchronous-trace-XXXXXX");
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("partial.json");
  QFile partial(path);
  QVERIFY(!persistSynchronousTrace(QByteArray("receipt"),
    [&] { return partial.open(QIODevice::WriteOnly | QIODevice::NewOnly); },
    [&](const QByteArray& value) { return partial.write(value.left(3)); }, [&] { return partial.flush(); }));
  partial.close();
  QVERIFY(!persistSynchronousTraceFile(path, "replacement"));
  QFile file(path);
  QVERIFY(file.open(QIODevice::ReadOnly));
  QCOMPARE(file.readAll(), QByteArray("rec"));
}

QTEST_GUILESS_MAIN(TestSynchronousStopTrace)
#include "test_synchronous_stop_trace.moc"
