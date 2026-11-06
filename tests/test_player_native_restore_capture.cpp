#include "test_player_native_restore.h"
#include <FixtureRestartAdapter.h>
#include <RestartWire.h>
#include "fixture_restart/OwnedFixtureFile.h"
#include <QCryptographicHash>
#include <QTemporaryFile>

void TestPlayerNativeRestore::fixtureBridgeCapture()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player;
  player.m_mpvGeneration = 3;
  player.m_receiptDispatch = [](quint64, const QString&) { return 0; };
  QVERIFY(!ownedFixtureTemplate().isEmpty());
  QTemporaryFile file(ownedFixtureTemplate());
  QVERIFY(file.open());
  const QByteArray data("fixture-a");
  QCOMPARE(file.write(data), qint64(data.size()));
  QVERIFY(file.flush());
  const auto hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
  FixtureRestart::ChildGate gate(&player, 100);
  FixtureRestartAdapter adapter(player, gate, 4, 1);
  QVERIFY(adapter.loadA(file.fileName(), hash, false));
  loadReplyAndStart(player, 1, 42);
  const auto key = player.m_nativeReceipts.snapshot().key;
  FixtureRestart::Grant binding{1, 2, key.revision, hash, 3000, false};
  FixtureRestart::Evidence frozen;
  QVERIFY(player.recordFixturePosition(PlayerComponent::FixtureAsset::TestClip, key, 42, 3000));
  QVERIFY(!adapter.captureA(binding, frozen));
  player.m_observationDispatch = [](quint64, const char**) { return 0; };
  QVERIFY(player.requestNativeObservation(key, 9, 10000, false));
  observationReply(player, 9, "NR1|1|42|yes|yes|3|no|no|no");
  QVERIFY(adapter.captureA(binding, frozen));
  QVERIFY(FixtureRestart::validateFixtureEvidence(frozen));
  QVERIFY(file.seek(file.size()));
  QCOMPARE(file.write("x", 1), qint64(1));
  QVERIFY(file.flush());
#ifdef HEALTHY_COPIED_SOURCE_TEST
  QVERIFY(player.healthyInstallRetirementClaim(std::make_shared<HealthyRetirement::RetirementClaim>(9), 9));
  player.m_healthyStopClock = [] { return qint64{1}; };
  player.m_healthyStopDispatch = [](quint64, const char**, bool) { return 0; };
  QVERIFY(!adapter.beginInternalReplacementStop(frozen, 100));
#else
  QVERIFY(!adapter.internalReplacementStop(frozen));
#endif
  QVERIFY(file.resize(data.size()));
  QVERIFY(file.flush());
  const auto revision = player.m_playbackIntent.snapshot().revision;
#ifdef HEALTHY_COPIED_SOURCE_TEST
  QVERIFY(adapter.beginInternalReplacementStop(frozen, 100));
  mpv_event event{}; event.event_id = MPV_EVENT_COMMAND_REPLY; event.reply_userdata = (quint64{1} << 60) | 1;
  player.observeHealthyReplacementStop(&event);
  mpv_event_end_file ended{}; ended.playlist_entry_id = 42; ended.reason = MPV_END_FILE_REASON_STOP;
  event = {}; event.event_id = MPV_EVENT_END_FILE; event.data = &ended;
  QVERIFY(player.observeHealthyReplacementStop(&event));
  QCOMPARE(adapter.pollInternalReplacementStop(), HealthyRestart::AsyncStop::Status::Pending);
  int idle = 1; mpv_event_property property{}; property.name = "idle-active";
  property.format = MPV_FORMAT_FLAG; property.data = &idle;
  event = {}; event.event_id = MPV_EVENT_GET_PROPERTY_REPLY; event.reply_userdata = (quint64{1} << 57) | 1; event.data = &property;
  player.observeHealthyReplacementStop(&event);
  QCOMPARE(adapter.pollInternalReplacementStop(), HealthyRestart::AsyncStop::Status::Complete);
#else
  player.m_stopDispatch = [] { return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Accepted, 0}; };
  QVERIFY(adapter.internalReplacementStop(frozen));
#endif
  QCOMPARE(player.m_playbackIntent.snapshot().revision, revision);
  QVERIFY(player.m_nativeReceipts.snapshot().invalid);
#ifdef HEALTHY_COPIED_SOURCE_TEST
  QVERIFY(!adapter.beginInternalReplacementStop(frozen, 100));
#else
  QVERIFY(!adapter.internalReplacementStop(frozen));
#endif
#endif
}
