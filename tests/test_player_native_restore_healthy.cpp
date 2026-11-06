#include "test_player_native_restore.h"
#ifdef HEALTHY_COPIED_SOURCE_TEST
#include <mpv/client.h>
#include <vector>

namespace { bool closeGate(void*) { return true; } }

std::shared_ptr<FixtureRestart::ChildGate> TestPlayerNativeRestore::bindCleanup(PlayerComponent& player)
{
  auto gate = std::make_shared<FixtureRestart::ChildGate>(&player, 100);
  player.m_healthyStopClock = [] { return qint64(10); };
  if (!player.healthyBindRestoreCleanupGate(gate)) return {};
  return gate;
}

bool TestPlayerNativeRestore::beginCleanup(PlayerComponent& player,
    const std::shared_ptr<FixtureRestart::ChildGate>& gate)
{
  return gate && gate->cancel(closeGate, nullptr) && player.beginNativeRestoreCancellation(gate, 1, 100);
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
