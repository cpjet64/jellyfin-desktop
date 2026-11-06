#include "test_player_native_restore.h"
#ifdef HEALTHY_COPIED_SOURCE_TEST
#include <mpv/client.h>
#include <QCryptographicHash>
#include <vector>

std::unique_ptr<TestPlayerNativeRestore::CleanupHarness> TestPlayerNativeRestore::armCleanup(
    PlayerComponent& player, const PlayerComponent::RestoreGrant& grant)
{
  using Child = FixtureRestart::RestartChild;
  auto harness = std::make_unique<CleanupHarness>();
  harness->gate = std::make_shared<FixtureRestart::ChildGate>(&player, 100);
  player.m_healthyStopClock = [] { return qint64(10); };
  if (!player.healthyBindRestoreCleanupGate(harness->gate, 100)) return {};
  const FixtureRestart::Grant binding{1, grant.id, grant.parentRevision, QByteArray(64, 'a'), grant.positionMs, grant.desiredPaused};
  const QByteArray frame = FixtureRestart::encodeGrant(binding);
  const QByteArray digest = QCryptographicHash::hash(frame, QCryptographicHash::Sha256);
  Child::Actions actions; actions.now = [] { return qint64(10); };
  actions.armActual = [&player, grant] {
    if (!player.armNativeRestore(grant, 7, 2)) return Child::ArmResult{};
    const auto key = player.m_nativeRestore.key;
    return Child::ArmResult{Child::ArmResult::Status::Armed, {key.host, key.core, key.revision, key.attempt}};
  };
  const auto gate = harness->gate;
  actions.beginCleanup = [&player, gate](quint64 sequence, quint64 revision) {
    return player.beginNativeRestoreCleanup(gate, HealthyRestart::VerifiedCleanupCancel{sequence, revision},
                                            100, HealthyRestart::FailureCause::None);
  };
  actions.pollCleanup = [] { return Child::CleanupResult{Child::CleanupResult::Status::Pending, {}, FixtureRestart::CancelFailure::None}; };
  harness->child = std::make_unique<Child>(*gate, gate.get(), &player, binding.run, 9, digest, std::move(actions));
  QByteArray response;
  if (!harness->child->grant(gate.get(), frame, 10, true, response)) return {};
  auto claim = std::make_shared<HealthyRetirement::RetirementClaim>(1);
  if (!player.healthyInstallRetirementClaim(claim, 1)) return {};
  return harness;
}

bool TestPlayerNativeRestore::beginCleanup(PlayerComponent& player, CleanupHarness& harness)
{
  QByteArray response;
  return harness.child && harness.child->control(&player,
      FixtureRestart::encodeControl({FixtureRestart::ControlKind::UserStop, 1, 5}), response) && response.isEmpty();
}

void TestPlayerNativeRestore::completeNoLoad(PlayerComponent& player)
{
  quint64 idle = 0, position = 0; std::vector<mpv_event*> queue; std::size_t index = 0;
  mpv_event none{}; none.event_id = MPV_EVENT_NONE;
  player.m_healthyStopEventWait = [&]() { return index < queue.size() ? queue[index++] : &none; };
  player.m_healthyStopDispatch = [&](quint64 id, const char** args, bool) {
    if (QByteArray(args[0]) == "idle-active") idle = id;
    if (QByteArray(args[0]) == "playlist-pos") position = id;
    return 0;
  };
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Pending);
  QVERIFY(idle && position);
  int idleValue = 1; mpv_event_property idleProperty{"idle-active", MPV_FORMAT_FLAG, &idleValue};
  int64_t noEntry = -1; mpv_event_property positionProperty{"playlist-pos", MPV_FORMAT_INT64, &noEntry};
  mpv_event idleReply{MPV_EVENT_GET_PROPERTY_REPLY, 0, idle, &idleProperty};
  mpv_event positionReply{MPV_EVENT_GET_PROPERTY_REPLY, 0, position, &positionProperty};
  queue = {&idleReply, &positionReply}; index = 0;
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::NoLoad);
  player.m_healthyStopDispatch = {}; player.m_healthyStopEventWait = {};
}

void TestPlayerNativeRestore::completeStopped(PlayerComponent& player, qint64 entry)
{
  quint64 idle = 0, position = 0; std::vector<mpv_event*> queue; std::size_t index = 0;
  mpv_event none{}; none.event_id = MPV_EVENT_NONE;
  player.m_healthyStopEventWait = [&]() { return index < queue.size() ? queue[index++] : &none; };
  player.m_healthyStopDispatch = [&](quint64 id, const char** args, bool) {
    if (QByteArray(args[0]) == "idle-active") idle = id;
    if (QByteArray(args[0]) == "playlist-pos") position = id;
    return 0;
  };
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Pending);
  mpv_event stop{}; stop.event_id = MPV_EVENT_COMMAND_REPLY; stop.reply_userdata = (quint64{1} << 55) | 2;
  mpv_event_end_file endData{}; endData.playlist_entry_id = entry; endData.reason = MPV_END_FILE_REASON_STOP;
  mpv_event end{}; end.event_id = MPV_EVENT_END_FILE; end.data = &endData;
  queue = {&stop, &end}; index = 0;
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Pending);
  QVERIFY(idle && position);
  int idleValue = 1; mpv_event_property idleProperty{"idle-active", MPV_FORMAT_FLAG, &idleValue};
  int64_t noEntry = -1; mpv_event_property positionProperty{"playlist-pos", MPV_FORMAT_INT64, &noEntry};
  mpv_event idleReply{MPV_EVENT_GET_PROPERTY_REPLY, 0, idle, &idleProperty};
  mpv_event positionReply{MPV_EVENT_GET_PROPERTY_REPLY, 0, position, &positionProperty};
  queue = {&idleReply, &positionReply}; index = 0;
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Stopped);
  player.m_healthyStopDispatch = {}; player.m_healthyStopEventWait = {};
}
#endif
