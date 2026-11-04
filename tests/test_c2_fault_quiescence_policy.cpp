#include <QtTest>
#include "c2_fault_quiescence_policy.h"

class TestC2FaultQuiescencePolicy : public QObject
{
  Q_OBJECT
private slots:
  void matchingStop();
};

void TestC2FaultQuiescencePolicy::matchingStop()
{
  const std::vector<C2TerminalEvent> events{{MPV_EVENT_START_FILE, 42}, {MPV_EVENT_FILE_LOADED},
    {MPV_EVENT_END_FILE, 42, MPV_END_FILE_REASON_STOP, 0}};
  QVERIFY(c2QuiescenceStop(events, 42));
  QVERIFY(!c2QuiescenceStop(events, 0));
  QVERIFY(!c2QuiescenceStop(events, 43));
  auto changed = events;
  changed.pop_back();
  QVERIFY(!c2QuiescenceStop(changed, 42));
  changed = events;
  changed.back().reason = MPV_END_FILE_REASON_ERROR;
  changed.back().error = -1;
  QVERIFY(!c2QuiescenceStop(changed, 42));
  changed = events;
  changed.back().insertCount = 1;
  QVERIFY(!c2QuiescenceStop(changed, 42));
  changed = events;
  changed.push_back({MPV_EVENT_START_FILE, 42});
  QVERIFY(!c2QuiescenceStop(changed, 42));
  changed = events;
  changed.push_back({MPV_EVENT_QUEUE_OVERFLOW});
  QVERIFY(!c2QuiescenceStop(changed, 42));
}

QTEST_GUILESS_MAIN(TestC2FaultQuiescencePolicy)
#include "test_c2_fault_quiescence_policy.moc"
