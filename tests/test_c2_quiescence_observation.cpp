#include <QtTest>
#include "c2_quiescence_observation.h"
#include "c2_quiescence_event_trace.h"
#include "c2_quiescence_terminal_predicate.h"

class TestC2QuiescenceObservation : public QObject
{
  Q_OBJECT
private slots:
  void immediateAndShortCircuit();
  void timeoutAndRecovery();
  void rejectedEventsAndBounds();
  void actualOrderedEvents();
  void terminalShortCircuit();
  void writeFailure();
};

void TestC2QuiescenceObservation::immediateAndShortCircuit()
{
  C2QuiescenceObservation observation;
  int calls = 0;
  const bool result = observation.check("first", [] { return false; }) &&
    observation.check("never", [&] { ++calls; return true; });
  QJsonObject saved;
  QVERIFY(!observation.finish(result, [&](const QJsonObject& record) { saved = record; return true; }));
  QCOMPARE(calls, 0);
  QCOMPARE(saved.value("exit_reason").toString(), QStringLiteral("immediate-rejection"));
  QVERIFY(!saved.value("phases").toObject().contains("never"));
}

void TestC2QuiescenceObservation::terminalShortCircuit()
{
  int canceled = 0, failures = 0, binding = 0, events = 0;
  const auto c = [&] { ++canceled; return true; };
  const auto f = [&] { ++failures; return true; };
  const auto b = [&] { ++binding; return false; };
  const auto e = [&] { ++events; return true; };
  QVERIFY(!c2QuiescenceTerminalPredicate(-1, 0, MPV_ERROR_PROPERTY_UNAVAILABLE, 0, true, c, f, b, e));
  QCOMPARE(canceled, 0); QCOMPARE(failures, 0); QCOMPARE(binding, 0); QCOMPARE(events, 0);
  QVERIFY(!c2QuiescenceTerminalPredicate(0, 1, MPV_ERROR_PROPERTY_UNAVAILABLE, 0, true, c, f, b, e));
  QCOMPARE(canceled, 1); QCOMPARE(failures, 1); QCOMPARE(binding, 1); QCOMPARE(events, 0);
}

void TestC2QuiescenceObservation::actualOrderedEvents()
{
  using Event = C2EventObserver::Event;
  std::vector<Event> events{{MPV_EVENT_START_FILE, 42}, {MPV_EVENT_FILE_LOADED},
    {MPV_EVENT_END_FILE, 42, MPV_END_FILE_REASON_ERROR, -7}};
  QJsonObject trace;
  int accepted = 0;
  QVERIFY(!c2EvaluateStopEvents(events, 0, 42, &trace, [&](const Event&) { ++accepted; }));
  QCOMPARE(accepted, 0);
  QCOMPARE(trace.value("rejection").toString(), QStringLiteral("unexpected-end"));
  QCOMPARE(trace.value("rejected_event").toObject().value("error").toInt(), -7);
  events.back().reason = MPV_END_FILE_REASON_STOP;
  events.back().error = 0;
  trace = {};
  QVERIFY(c2EvaluateStopEvents(events, 0, 42, &trace, [&](const Event&) { ++accepted; }));
  QCOMPARE(accepted, 1);
  QVERIFY(trace.value("accepted").toBool());
}

void TestC2QuiescenceObservation::timeoutAndRecovery()
{
  C2QuiescenceObservation timed;
  const auto timeout = [](const std::function<bool()>& test) { test(); test(); return false; };
  QVERIFY(!timed.wait("terminal", timeout, [] { return false; }));
  const auto timedRecord = timed.record(false);
  QCOMPARE(timedRecord.value("exit_reason").toString(), QStringLiteral("wait-timeout"));
  QCOMPARE(timedRecord.value("phases").toObject().value("terminal").toObject().value("count").toInt(), 2);
  C2QuiescenceObservation recovered;
  int samples = 0;
  const auto run = [](const std::function<bool()>& test) { return test() || test() || test(); };
  QVERIFY(recovered.wait("terminal", run, [&] { return ++samples == 3; }));
  QJsonObject saved;
  QVERIFY(recovered.finish(true, [&](const QJsonObject& record) { saved = record; return true; }));
  const auto phase = saved.value("phases").toObject().value("terminal").toObject();
  QCOMPARE(phase.value("count").toInt(), 3);
  QVERIFY(!phase.value("first").toObject().value("result").toBool());
  QVERIFY(phase.value("last").toObject().value("result").toBool());
  QCOMPARE(saved.value("exit_reason").toString(), QStringLiteral("success"));
}

void TestC2QuiescenceObservation::rejectedEventsAndBounds()
{
  QJsonObject trace;
  QJsonArray events;
  for (int i = 0; i < 40; ++i) c2TraceEvent(trace, events, {{"index", i}});
  c2TraceRejection(trace, 39, "unexpected-end", {{"entry", 77}, {"error", -4}});
  QCOMPARE(trace.value("events").toArray().size(), 32);
  QVERIFY(trace.value("events_truncated").toBool());
  QCOMPARE(trace.value("rejected_event").toObject().value("entry").toInt(), 77);
  C2QuiescenceObservation observation;
  observation.sample("events", trace);
  observation.sample("oversize", {{"value", QString(20000, 'x')}});
  for (int i = 0; i < 80; ++i) observation.value(QString::number(i), i);
  const auto record = observation.record(false);
  QVERIFY(record.value("truncated").toBool());
  QVERIFY(QJsonDocument(record).toJson(QJsonDocument::Compact).size() < 50000);
  QCOMPARE(record.value("phases").toObject().value("events").toObject().value("last").toObject()
    .value("rejected_event").toObject().value("error").toInt(), -4);
}

void TestC2QuiescenceObservation::writeFailure()
{
  C2QuiescenceObservation observation;
  QVERIFY(!observation.check("stop-status", [] { return false; }));
  QJsonObject attempted;
  QString fallbackPhase, fallbackReason;
  QVERIFY(!observation.finish(false, [&](const QJsonObject& value) { attempted = value; return false; },
    [&](const QString& phase, const QString& reason) { fallbackPhase = phase; fallbackReason = reason; }));
  QCOMPARE(fallbackPhase, QStringLiteral("stop-status"));
  QCOMPARE(fallbackReason, QStringLiteral("immediate-rejection"));
  QCOMPARE(attempted.value("exit_stage").toString(), QStringLiteral("stop-status"));
  QCOMPARE(attempted.value("exit_reason").toString(), QStringLiteral("immediate-rejection"));
  QVERIFY(!attempted.value("original_verdict").toBool());
  C2QuiescenceObservation successful;
  QVERIFY(!successful.finish(true, [](const QJsonObject&) { return false; }));
}

QTEST_GUILESS_MAIN(TestC2QuiescenceObservation)
#include "test_c2_quiescence_observation.moc"
