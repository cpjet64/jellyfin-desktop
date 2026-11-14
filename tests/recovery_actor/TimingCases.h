#pragma once

// These member definitions share the actor fixture declared by the test TU.
void TestPlayerRecoveryActor::retryNearClockLimit()
{
  Fixture f;
  QSignalSpy failures(&f.player, &PlayerComponent::loadPreparationFailed);
  const auto limit = std::numeric_limits<std::int64_t>::max();
  f.now = limit - 1500;
  f.replace();
  QCOMPARE(f.queries.size(), std::size_t{1});
  QCOMPARE(f.actor->ordinaryLoad_.deadlineMs, limit);
  f.events.playlistError(f.queries.back()); f.wake();
  f.now = limit - 1; f.actor->tick();
  QCOMPARE(f.queries.size(), std::size_t{2});
  QCOMPARE(f.actor->ordinaryLoad_.deadlineMs, limit);
  QVERIFY(f.commands.empty());
  f.now = limit; f.actor->tick();
  QCOMPARE(f.queries.size(), std::size_t{2});
  QCOMPARE(failures.size(), 1);
  QCOMPARE(failures[0][1].toString(), QStringLiteral("preparation-timeout"));
  f.events.playlistReply(f.queries.back(), {}); f.wake();
  QVERIFY(f.commands.empty());
  QCOMPARE(f.queries.size(), std::size_t{2});
}

void TestPlayerRecoveryActor::preparationClockRegression_data()
{
  QTest::addColumn<bool>("onReply");
  QTest::newRow("tick") << false;
  QTest::newRow("query-reply") << true;
}

void TestPlayerRecoveryActor::preparationClockRegression()
{
  QFETCH(bool, onReply);
  Fixture f;
  QSignalSpy failures(&f.player, &PlayerComponent::loadPreparationFailed);
  f.now = 10000; f.replace();
  const auto query = f.queries.back();
  const auto deadline = f.actor->ordinaryLoad_.deadlineMs;
  f.now = 10500; f.actor->tick();
  QCOMPARE(f.queries.size(), std::size_t{1});
  QCOMPARE(f.actor->ordinaryLoad_.deadlineMs, deadline);
  f.now = 10400;
  if (onReply) { f.events.playlistReply(query, {}); f.wake(); }
  else f.actor->tick();
  QCOMPARE(failures.size(), 1);
  QCOMPARE(failures[0][1].toString(), QStringLiteral("preparation-unavailable"));
  QVERIFY(f.commands.empty());
  f.now = deadline; f.actor->tick();
  f.events.playlistReply(query, {}); f.wake();
  QCOMPARE(f.queries.size(), std::size_t{1});
  QCOMPARE(failures.size(), 1);
  QVERIFY(f.commands.empty());
}
