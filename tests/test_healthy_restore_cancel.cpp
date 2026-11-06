#include "player/PlayerComponent.h"
#include "player_test_cache.h"
#include <RestartBroker.h>
#include <RestartChild.h>
#include <QCryptographicHash>
#include <QtTest/QtTest>
#include <map>
#ifdef HEALTHY_RETIREMENT_TESTING
#include "healthy_restore_claim_test.h"
#endif

class TestHealthyRestoreCleanup : public QObject
{
  Q_OBJECT
  PlayerTestCache cache_;
  using Gate = FixtureRestart::ChildGate;
  using Status = PlayerComponent::RestoreCleanupStatus;
  static bool checked(void*) { return true; }
  struct Harness { std::shared_ptr<Gate> gate; std::unique_ptr<FixtureRestart::RestartChild> child; };
  std::map<PlayerComponent*, std::unique_ptr<Harness>> harnesses_;
  std::shared_ptr<Gate> prepare(PlayerComponent& player, int& stops, bool load);
  static void event(PlayerComponent& player, mpv_event& value);
  static void loadReply(PlayerComponent& player, qint64 entry);
  static void start(PlayerComponent& player, qint64 entry);
  static void reply(PlayerComponent& player, quint64 tag, int error = 0);
  static void end(PlayerComponent& player, qint64 entry, mpv_end_file_reason reason);
  static void property(PlayerComponent& player, quint64 tag, const char* name, mpv_format format, void* data);
  bool cancel(PlayerComponent& player, const std::shared_ptr<Gate>& gate);
private slots:
  void beforeLoad();
  void noLoadRejectsUnsolicitedEvents();
  void reentrantQueriesRequireLaterDrain();
  void canceledGateCannotBind();
  void submittedWithoutStart();
  void bufferedStartBeforeReply();
  void lateEntryDuringTerminalQuery();
  void terminalOrdering();
  void pendingSeekBlocksSettlement();
  void rejectedAndWrongReplies();
  void duplicateAndReentrant();
  void supersededAndEventLoss();
  void missingIdentityAndRedirect();
  void disabledConfiguration();
};

std::shared_ptr<TestHealthyRestoreCleanup::Gate>
TestHealthyRestoreCleanup::prepare(PlayerComponent& player, int& stops, bool load)
{
  auto gate = std::make_shared<Gate>(&player, 100);
#ifdef MPVQT_EXTERNAL_EVENTS
  auto harness = std::make_unique<Harness>(); harness->gate = gate;
  if (!player.bindNativeReceiptHost(7)) return {};
  player.m_mpvGeneration = 8;
  const FixtureRestart::Grant binding{1, 3, 4, QByteArray(64, 'a'), 3000, true};
  const auto frame = FixtureRestart::encodeGrant(binding);
  const auto digest = QCryptographicHash::hash(frame, QCryptographicHash::Sha256);
  player.m_healthyStopClock = [] { return qint64{1}; };
  player.m_restoreDispatch = [&](quint64, const char** args) {
    if (QByteArray(args[0]) == "stop") ++stops;
    return 0;
  };
  player.m_healthyStopDispatch = [](quint64, const char**, bool) { return 0; };
  if (!player.healthyBindRestoreCleanupGate(gate, 100)) return {};
  const PlayerComponent::RestoreGrant grant{3, 4, 0, 3000, true};
  auto claim = TestRetirementClaim::reserve(9); if (!claim) return {};
  using Child = FixtureRestart::RestartChild;
  Child::Actions actions; actions.now = [] { return qint64{1}; };
  actions.armActual = [&player, grant, claim] {
    if (!player.armNativeRestore(grant, 7, 2)) return Child::ArmResult{};
    const auto key = player.m_nativeRestore.key;
    const HealthyRetirement::RetirementClaim::ArmIdentity identity{key.host, key.core, key.revision, key.attempt};
    if (!TestRetirementClaim::bind(claim, 9, identity) ||
        !player.healthyInstallRetirementClaim(claim, 9, HealthyRestart::ClaimInstallMode::BindRestoreReservation) ||
        !TestRetirementClaim::qualify(claim, 9, identity)) return Child::ArmResult{};
    return Child::ArmResult{Child::ArmResult::Status::Armed, {key.host, key.core, key.revision, key.attempt}};
  };
  actions.beginCleanup = [&player, gate](quint64 sequence, quint64 revision) {
    return player.beginNativeRestoreCleanup(gate, HealthyRestart::VerifiedCleanupCancel{sequence, revision},
                                            100, HealthyRestart::FailureCause::None);
  };
  actions.pollCleanup = [] { return Child::CleanupResult{Child::CleanupResult::Status::Pending, {}, FixtureRestart::CancelFailure::None}; };
  harness->child = std::make_unique<Child>(*gate, &player, &player, binding.run, 9, digest, std::move(actions));
  QByteArray response;
  if (!harness->child->grant(&player, frame, 1, true, response)) return {};
  harnesses_[&player] = std::move(harness);
  if (load && !player.dispatchNativeRestoreLoad(QStringLiteral("fixture"))) return {};
#else
  Q_UNUSED(player); Q_UNUSED(stops); Q_UNUSED(load);
#endif
  return gate;
}

void TestHealthyRestoreCleanup::event(PlayerComponent& player, mpv_event& value)
{
  player.observeNativeRestoreCleanup(&value);
  player.observeNativeReceipt(&value);
}

void TestHealthyRestoreCleanup::loadReply(PlayerComponent& player, qint64 entry)
{
  mpv_node node{}; node.format = MPV_FORMAT_INT64; node.u.int64 = entry;
  char name[] = "playlist_entry_id"; char* names[] = {name};
  mpv_node_list list{}; list.num = 1; list.keys = names; list.values = &node;
  mpv_event_command command{}; command.result.format = MPV_FORMAT_NODE_MAP; command.result.u.list = &list;
  mpv_event value{}; value.event_id = MPV_EVENT_COMMAND_REPLY;
  value.reply_userdata = (quint64{1} << 63) | 2; value.data = &command;
  event(player, value);
}

void TestHealthyRestoreCleanup::start(PlayerComponent& player, qint64 entry)
{
  mpv_event_start_file data{}; data.playlist_entry_id = entry;
  mpv_event value{}; value.event_id = MPV_EVENT_START_FILE; value.data = &data; event(player, value);
}

void TestHealthyRestoreCleanup::reply(PlayerComponent& player, quint64 tag, int error)
{
  mpv_event value{}; value.event_id = MPV_EVENT_COMMAND_REPLY;
  value.reply_userdata = tag | 2; value.error = error; event(player, value);
}

void TestHealthyRestoreCleanup::end(PlayerComponent& player, qint64 entry, mpv_end_file_reason reason)
{
  mpv_event_end_file data{}; data.playlist_entry_id = entry; data.reason = reason;
  mpv_event value{}; value.event_id = MPV_EVENT_END_FILE; value.data = &data; event(player, value);
}

void TestHealthyRestoreCleanup::property(PlayerComponent& player, quint64 tag, const char* name,
                                         mpv_format format, void* data)
{
  mpv_event_property prop{}; prop.name = name; prop.format = format; prop.data = data;
  mpv_event value{}; value.event_id = MPV_EVENT_GET_PROPERTY_REPLY;
  value.reply_userdata = tag | 2; value.data = &prop; event(player, value);
}

bool TestHealthyRestoreCleanup::cancel(PlayerComponent& player, const std::shared_ptr<Gate>& gate)
{
  const auto it = harnesses_.find(&player);
  if (!gate || it == harnesses_.end() || it->second->gate != gate) return false;
  QByteArray response;
  return it->second->child->control(&player,
      FixtureRestart::encodeControl({FixtureRestart::ControlKind::UserStop, 11, 5}), response) && response.isEmpty();
}

void TestHealthyRestoreCleanup::beforeLoad()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player; int stops = 0; auto gate = prepare(player, stops, false); QVERIFY(gate);
  QVERIFY(cancel(player, gate)); QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
  auto wrong = std::make_shared<Gate>(&player, 100); QVERIFY(wrong->cancel(checked, nullptr));
  QVERIFY(!player.beginNativeRestoreCleanup(wrong, HealthyRestart::VerifiedCleanupCancel{11, 5},
                                            100, HealthyRestart::FailureCause::None));
  QCOMPARE(stops, 0);
  int idle = 1; qint64 position = -1;
  property(player, quint64{1} << 54, "idle-active", MPV_FORMAT_FLAG, &idle);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
  property(player, quint64{1} << 53, "playlist-pos", MPV_FORMAT_INT64, &position);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::NoLoad);
  QCOMPARE(stops, 0); QVERIFY(!player.dispatchNativeRestoreLoad(QStringLiteral("fixture")));
#endif
}

void TestHealthyRestoreCleanup::noLoadRejectsUnsolicitedEvents()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  for (int kind = 0; kind != 3; ++kind) {
    PlayerComponent player; int stops = 0; auto gate = prepare(player, stops, false); QVERIFY(gate);
    QVERIFY(cancel(player, gate)); QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
    if (kind == 0) start(player, 42);
    else if (kind == 1) end(player, 42, MPV_END_FILE_REASON_STOP);
    else reply(player, quint64{1} << 55);
    QCOMPARE(player.pollNativeRestoreCancellation(), Status::Failed); QCOMPARE(stops, 0);
  }
#endif
}

void TestHealthyRestoreCleanup::canceledGateCannotBind()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player; auto gate = std::make_shared<Gate>(&player, 100);
  QVERIFY(gate->cancel(checked, nullptr));
  QVERIFY(!player.healthyBindRestoreCleanupGate(gate, 100));
#endif
}

void TestHealthyRestoreCleanup::reentrantQueriesRequireLaterDrain()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player; int stops = 0; auto gate = prepare(player, stops, false); QVERIFY(gate);
  mpv_event none{}; none.event_id = MPV_EVENT_NONE; int drains = 0;
  player.m_healthyStopEventWait = [&]() -> mpv_event* { ++drains; return &none; };
  int idle = 1; qint64 position = -1; int queries = 0;
  player.m_healthyStopDispatch = [&](quint64 id, const char**, bool query) {
    if (!query) return -1;
    ++queries;
    if (id == ((quint64{1} << 54) | 2))
      property(player, quint64{1} << 54, "idle-active", MPV_FORMAT_FLAG, &idle);
    else property(player, quint64{1} << 53, "playlist-pos", MPV_FORMAT_INT64, &position);
    return 0;
  };
  QVERIFY(cancel(player, gate));
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
  QCOMPARE(drains, 1); QCOMPARE(queries, 2); QCOMPARE(stops, 0);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::NoLoad);
  QCOMPARE(drains, 2); QCOMPARE(stops, 0);
#endif
}

void TestHealthyRestoreCleanup::submittedWithoutStart()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player; int stops = 0; auto gate = prepare(player, stops, true); QVERIFY(gate);
  qint64 now = 1; player.m_healthyStopClock = [&] { return now; };
  QVERIFY(cancel(player, gate)); QCOMPARE(stops, 0);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending); QCOMPARE(stops, 0);
  loadReply(player, 42);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending); QCOMPARE(stops, 1);
  reply(player, quint64{1} << 55);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
  int idle = 1; qint64 position = -1;
  property(player, quint64{1} << 54, "idle-active", MPV_FORMAT_FLAG, &idle);
  property(player, quint64{1} << 53, "playlist-pos", MPV_FORMAT_INT64, &position);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
  now = 100; QCOMPARE(player.pollNativeRestoreCancellation(), Status::Failed);
#endif
}

void TestHealthyRestoreCleanup::bufferedStartBeforeReply()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player; int stops = 0; auto gate = prepare(player, stops, true); QVERIFY(gate);
  mpv_event_start_file data{}; data.playlist_entry_id = 42;
  mpv_event started{}; started.event_id = MPV_EVENT_START_FILE; started.data = &data;
  mpv_event none{}; none.event_id = MPV_EVENT_NONE; int reads = 0; bool stopAfterDrain = true;
  player.m_healthyStopEventWait = [&]() -> mpv_event* {
    if (reads < 2 && stops != 0) stopAfterDrain = false;
    return reads++ == 0 ? &started : &none;
  };
  QVERIFY(cancel(player, gate)); QCOMPARE(stops, 0);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending); QCOMPARE(stops, 0); QVERIFY(stopAfterDrain);
  loadReply(player, 42); QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending); QCOMPARE(stops, 1);
  reply(player, quint64{1} << 55);
  end(player, 42, MPV_END_FILE_REASON_STOP);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
  int idle = 1; qint64 position = -1;
  property(player, quint64{1} << 54, "idle-active", MPV_FORMAT_FLAG, &idle);
  property(player, quint64{1} << 53, "playlist-pos", MPV_FORMAT_INT64, &position);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Stopped);
#endif
}

void TestHealthyRestoreCleanup::lateEntryDuringTerminalQuery()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  for (bool starting : {false, true}) {
    PlayerComponent player; int stops = 0; auto gate = prepare(player, stops, true); QVERIFY(gate);
    loadReply(player, 42); QVERIFY(cancel(player, gate));
    QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
    reply(player, quint64{1} << 55);
    QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
    if (starting) start(player, 42); else end(player, 42, MPV_END_FILE_REASON_STOP);
    QCOMPARE(player.pollNativeRestoreCancellation(), Status::Failed);
  }
#endif
}

void TestHealthyRestoreCleanup::terminalOrdering()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  for (bool endFirst : {false, true}) {
    PlayerComponent player; int stops = 0; auto gate = prepare(player, stops, true); QVERIFY(gate);
    loadReply(player, 42); start(player, 42); QVERIFY(cancel(player, gate));
    QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
    if (endFirst) end(player, 42, MPV_END_FILE_REASON_STOP);
    reply(player, quint64{1} << 55);
    QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
    if (!endFirst) end(player, 42, MPV_END_FILE_REASON_STOP);
    QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
    int idle = 1; qint64 position = -1;
    property(player, quint64{1} << 54, "idle-active", MPV_FORMAT_FLAG, &idle);
    QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
    property(player, quint64{1} << 53, "playlist-pos", MPV_FORMAT_INT64, &position);
    QCOMPARE(player.pollNativeRestoreCancellation(), Status::Stopped); QCOMPARE(stops, 1);
  }
#endif
}

void TestHealthyRestoreCleanup::rejectedAndWrongReplies()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent rejected; int stops = 0; auto gate = prepare(rejected, stops, true); QVERIFY(gate);
  loadReply(rejected, 42);
  rejected.m_restoreDispatch = [&](quint64, const char**) { ++stops; return -1; };
  QVERIFY(cancel(rejected, gate)); QCOMPARE(rejected.pollNativeRestoreCancellation(), Status::Failed);
  PlayerComponent wrong; stops = 0; gate = prepare(wrong, stops, true); QVERIFY(gate);
  loadReply(wrong, 42);
  QVERIFY(cancel(wrong, gate)); QCOMPARE(wrong.pollNativeRestoreCancellation(), Status::Pending);
  reply(wrong, quint64{1} << 56);
  QCOMPARE(wrong.pollNativeRestoreCancellation(), Status::Pending);
  reply(wrong, quint64{1} << 55, -1); QCOMPARE(wrong.pollNativeRestoreCancellation(), Status::Failed);
#endif
}

void TestHealthyRestoreCleanup::pendingSeekBlocksSettlement()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player; int stops = 0; auto gate = prepare(player, stops, true); QVERIFY(gate);
  loadReply(player, 42); start(player, 42);
  player.m_observationDispatch = [](quint64, const char**) { return 0; };
  const auto key = player.m_nativeReceipts.snapshot().key;
  QVERIFY(player.requestNativeObservation(key, 1, 10000, true));
  QByteArray text("NR1|1|42|yes|yes|0|yes|no|no");
  mpv_event_command command{}; command.result.format = MPV_FORMAT_STRING;
  command.result.u.string = text.data();
  mpv_event observation{}; observation.event_id = MPV_EVENT_COMMAND_REPLY;
  observation.reply_userdata = (quint64{1} << 62) | 1; observation.data = &command;
  event(player, observation); QVERIFY(player.dispatchNativeRestoreSeek());
  QVERIFY(cancel(player, gate)); QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
  QCOMPARE(stops, 0);
  reply(player, quint64{1} << 61);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending); QCOMPARE(stops, 1);
  end(player, 42, MPV_END_FILE_REASON_STOP); reply(player, quint64{1} << 55);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
  int idle = 1; qint64 position = -1;
  property(player, quint64{1} << 54, "idle-active", MPV_FORMAT_FLAG, &idle);
  property(player, quint64{1} << 53, "playlist-pos", MPV_FORMAT_INT64, &position);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Stopped);
#endif
}

void TestHealthyRestoreCleanup::duplicateAndReentrant()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player; int stops = 0; auto gate = prepare(player, stops, true); QVERIFY(gate);
  loadReply(player, 42);
  bool nestedAccepted = false;
  player.m_restoreDispatch = [&](quint64, const char**) {
    ++stops; nestedAccepted = cancel(player, gate); return 0;
  };
  QVERIFY(cancel(player, gate)); QCOMPARE(stops, 0);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
  QCOMPARE(stops, 1); QVERIFY(!nestedAccepted);
  QVERIFY(!cancel(player, gate));
  reply(player, quint64{1} << 55); reply(player, quint64{1} << 55);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Failed); QCOMPARE(stops, 1);
#endif
}

void TestHealthyRestoreCleanup::supersededAndEventLoss()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  for (bool core : {false, true}) {
    PlayerComponent player; int stops = 0; auto gate = prepare(player, stops, true); QVERIFY(gate);
    if (core) ++player.m_mpvGeneration; else player.m_playbackIntent.play(false);
    const auto owner = player.m_playbackIntent.snapshot();
    const auto generation = player.m_mpvGeneration;
    QVERIFY(!cancel(player, gate)); QCOMPARE(stops, 0);
    QVERIFY(gate->matchesVerifiedCancel(11, 5));
    QVERIFY(!player.m_restoreCleanup.claimed);
    QCOMPARE(player.pollNativeRestoreCancellation(), Status::Unstarted);
    const auto after = player.m_playbackIntent.snapshot();
    QCOMPARE(after.epoch, owner.epoch); QCOMPARE(after.request, owner.request);
    QCOMPARE(after.revision, owner.revision); QCOMPARE(after.desired, owner.desired);
    QCOMPARE(player.m_mpvGeneration, generation); QCOMPARE(stops, 0);
  }
  PlayerComponent player; int stops = 0; auto gate = prepare(player, stops, true); QVERIFY(gate);
  QVERIFY(cancel(player, gate)); mpv_event value{}; value.event_id = MPV_EVENT_QUEUE_OVERFLOW;
  event(player, value); QCOMPARE(player.pollNativeRestoreCancellation(), Status::Failed);
#endif
}

void TestHealthyRestoreCleanup::missingIdentityAndRedirect()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent missing; int stops = 0; auto gate = prepare(missing, stops, true); QVERIFY(gate);
  loadReply(missing, 0);
  QVERIFY(cancel(missing, gate)); QCOMPARE(missing.pollNativeRestoreCancellation(), Status::Pending);
  reply(missing, quint64{1} << 55); end(missing, 0, MPV_END_FILE_REASON_STOP);
  QCOMPARE(missing.pollNativeRestoreCancellation(), Status::Failed);
  PlayerComponent redirected; stops = 0; gate = prepare(redirected, stops, true); QVERIFY(gate);
  loadReply(redirected, 42); start(redirected, 42); QVERIFY(cancel(redirected, gate));
  QCOMPARE(redirected.pollNativeRestoreCancellation(), Status::Pending);
  end(redirected, 42, MPV_END_FILE_REASON_REDIRECT);
  QCOMPARE(redirected.pollNativeRestoreCancellation(), Status::Failed);
#endif
}

void TestHealthyRestoreCleanup::disabledConfiguration()
{
#ifndef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player; int stops = 0; auto gate = prepare(player, stops, false);
  QVERIFY(!cancel(player, gate)); QCOMPARE(stops, 0);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Failed);
#endif
}

QTEST_GUILESS_MAIN(TestHealthyRestoreCleanup)
#include "test_healthy_restore_cancel.moc"
