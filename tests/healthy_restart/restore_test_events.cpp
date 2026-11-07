#include "restore_test_fixture.h"
#include "restore_test_renderer.h"
#include "graph_session.h"
#include "fixture_restart/RestartWire.h"
#include "player/PlayerComponent.h"
#include <mpv/client.h>

bool HealthyRestoreTestFixture::proveNoLoadCleanup()
{
  using Progress = HealthyRetirement::PreparedGraph::FailureProgress;
  if (!prepared_ || !graph_ || !graph_->player_ || state_->loads || state_->stops ||
      !prepared_->beginRestoreFailure(HealthyRestart::FailureCause::ProtocolViolation) ||
      prepared_->pollRestoreFailure() != Progress::Pending || !state_->idleQuery || !state_->positionQuery) return false;
  auto& player = *graph_->player_;
  int idle = 1; mpv_event_property idleProperty{"idle-active", MPV_FORMAT_FLAG, &idle};
  mpv_event idleReply{MPV_EVENT_GET_PROPERTY_REPLY, 0, state_->idleQuery, &idleProperty};
  player.observeNativeRestoreCleanup(&idleReply); player.observeNativeReceipt(&idleReply);
  int64_t position = -1; mpv_event_property positionProperty{"playlist-pos", MPV_FORMAT_INT64, &position};
  mpv_event positionReply{MPV_EVENT_GET_PROPERTY_REPLY, 0, state_->positionQuery, &positionProperty};
  player.observeNativeRestoreCleanup(&positionReply); player.observeNativeReceipt(&positionReply);
  return prepared_->pollRestoreFailure() == Progress::Qualified && !state_->loads && !state_->stops;
}

bool HealthyRestoreTestFixture::replyLoad(qint64 entry, bool startFirst, bool withStart, int error)
{
  if (!graph_ || !graph_->session_ || !graph_->player_) return false;
  auto& player = *graph_->player_;
  mpv_node value{}; value.format = MPV_FORMAT_INT64; value.u.int64 = entry;
  char field[] = "playlist_entry_id"; char* fields[] = {field};
  mpv_node_list list{}; list.num = 1; list.keys = fields; list.values = &value;
  mpv_event_command command{}; command.result.format = MPV_FORMAT_NODE_MAP; command.result.u.list = &list;
  mpv_event reply{}; reply.event_id = MPV_EVENT_COMMAND_REPLY; reply.error = error;
  reply.reply_userdata = (quint64{1} << 63) | graph_->session_->attempt; reply.data = &command;
  mpv_event_start_file start{}; start.playlist_entry_id = entry;
  mpv_event started{}; started.event_id = MPV_EVENT_START_FILE; started.data = &start;
  if (withStart && startFirst) player.observeNativeReceipt(&started);
  player.observeNativeReceipt(&reply);
  if (withStart && !startFirst) player.observeNativeReceipt(&started);
  return player.m_nativeReceipts.snapshot().reply;
}

bool HealthyRestoreTestFixture::observe(quint64 id, qint64 entry, const QByteArray& position)
{
  if (!graph_ || !graph_->player_) return false;
  auto& player = *graph_->player_;
  if (!player.m_nativeObservation.pending &&
      !player.requestNativeObservation(player.m_nativeReceipts.snapshot().key, id, 10000, true)) return false;
  if (!player.m_nativeObservation.pending || player.m_nativeObservation.id != id) return false;
  QByteArray text = "NR1|1|" + QByteArray::number(entry) + "|yes|yes|" + position + "|yes|no|no";
  mpv_event_command command{}; command.result.format = MPV_FORMAT_STRING; command.result.u.string = text.data();
  mpv_event event{}; event.event_id = MPV_EVENT_COMMAND_REPLY;
  event.reply_userdata = (quint64{1} << 62) | id; event.data = &command;
  player.observeNativeReceipt(&event);
  return player.m_nativeObservation.valid;
}

bool HealthyRestoreTestFixture::startFile(qint64 entry)
{
  if (!graph_ || !graph_->player_) return false;
  mpv_event_start_file start{}; start.playlist_entry_id = entry;
  mpv_event event{}; event.event_id = MPV_EVENT_START_FILE; event.data = &start;
  graph_->player_->observeNativeReceipt(&event);
  return graph_->player_->m_nativeReceipts.snapshot().started;
}

bool HealthyRestoreTestFixture::cancel(quint64 sequence, quint64 revision, QByteArray& response)
{
  return graph_ && graph_->restoreControl(FixtureRestart::encodeControl(
      {FixtureRestart::ControlKind::UserStop, sequence, revision}), response);
}

bool HealthyRestoreTestFixture::pollCancellation(QByteArray& response)
{ return graph_ && graph_->restorePollCancellation(response); }

bool HealthyRestoreTestFixture::publishBaseline()
{
  return graph_ && renderer_ && graph_->captureBaseline() && renderer_->queue() && renderer_->publish(0);
}

bool HealthyRestoreTestFixture::armTarget()
{
  return graph_ && renderer_ && graph_->captureTarget() && renderer_->queue() && graph_->captureTargetReady();
}

void HealthyRestoreTestFixture::replySeekAndRestart(int error, bool restartFirst, bool restart)
{
  if (!graph_ || !graph_->session_ || !graph_->player_) return;
  auto& player = *graph_->player_;
  mpv_event restarted{}; restarted.event_id = MPV_EVENT_PLAYBACK_RESTART;
  if (restart && restartFirst) player.observeNativeReceipt(&restarted);
  mpv_event reply{}; reply.event_id = MPV_EVENT_COMMAND_REPLY; reply.error = error;
  reply.reply_userdata = (quint64{1} << 61) | graph_->session_->attempt;
  player.observeNativeReceipt(&reply);
  if (restart && !restartFirst) player.observeNativeReceipt(&restarted);
}

bool HealthyRestoreTestFixture::publishTarget(int index)
{ return renderer_ && renderer_->queue() && renderer_->publish(index); }

bool HealthyRestoreTestFixture::replaceFramebuffer()
{ return renderer_ && renderer_->replaceFramebuffer(); }
