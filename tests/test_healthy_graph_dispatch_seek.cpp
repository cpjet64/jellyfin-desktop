#include "test_healthy_graph_dispatch_fixture.h"

void TestHealthyGraphDispatch::permitThenLostEndpointRejectsDirectSeek()
{
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  DISPATCH_CHECK(fixture.graph->restoreLoad()); fixture.replyLoad(42);
  fixture.observe(1, 42, "0.000"); fixture.qualifyBaseline(); fixture.armTarget();
  fixture.live->endpoints = false;
  DISPATCH_CHECK(!fixture.graph->player_->dispatchNativeRestoreSeek());
  DISPATCH_CHECK(fixture.live->loads == 1 && fixture.live->seeks == 0);
  fixture.checkNoUnpause();
}

void TestHealthyGraphDispatch::revocationDuringSeekAuthorization()
{
  for (bool module : {false, true}) {
    Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
    DISPATCH_CHECK(fixture.graph->restoreLoad()); fixture.replyLoad(42);
    fixture.observe(1, 42, "0.000"); fixture.qualifyBaseline(); fixture.armTarget();
    auto state = fixture.live;
    if (module) state->moduleHook = [state] { state->module = false; };
    else state->endpointHook = [state] { state->endpoints = false; };
    DISPATCH_CHECK(!fixture.graph->player_->dispatchNativeRestoreSeek());
    DISPATCH_CHECK(state->loads == 1 && state->seeks == 0);
    fixture.checkNoUnpause();
  }
}

void TestHealthyGraphDispatch::clockInvalidatesTargetBeforeSeekAdmission()
{
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  DISPATCH_CHECK(fixture.graph->restoreLoad()); fixture.replyLoad(42);
  fixture.observe(1, 42, "0.000"); fixture.qualifyBaseline(); fixture.armTarget();
  auto* renderer = fixture.renderer.get(); bool fired = false;
  fixture.live->clockHook = [renderer, &fired] { fired = true; renderer->replaceFramebuffer(); };
  DISPATCH_CHECK(!fixture.graph->player_->dispatchNativeRestoreSeek());
  DISPATCH_CHECK(fired && fixture.live->loads == 1 && fixture.live->seeks == 0);
  fixture.checkNoUnpause();
}

void TestHealthyGraphDispatch::lastLivenessCallbackInvalidatesTarget()
{
  std::vector<char> callbacks;
  {
    Fixture control; control.initialize(); control.acceptGrantAndPermit();
    DISPATCH_CHECK(control.graph->restoreLoad()); control.replyLoad(42);
    control.observe(1, 42, "0.000"); control.qualifyBaseline(); control.armTarget();
    control.live->trace = true;
    DISPATCH_CHECK(control.graph->player_->dispatchNativeRestoreSeek());
    callbacks = control.live->callbacksAtSubmission;
    DISPATCH_CHECK(control.live->seeks == 1 && !callbacks.empty());
  }
  DISPATCH_CHECK(callbacks.back() == 'M' || callbacks.back() == 'E');
  DISPATCH_CHECK(std::find(callbacks.begin(), callbacks.end(), 'C') != callbacks.end());
  Fixture victim; victim.initialize(); victim.acceptGrantAndPermit();
  DISPATCH_CHECK(victim.graph->restoreLoad()); victim.replyLoad(42);
  victim.observe(1, 42, "0.000"); victim.qualifyBaseline(); victim.armTarget();
  auto state = victim.live; state->trace = true; state->triggerAt = callbacks.size();
  auto* renderer = victim.renderer.get();
  state->finalCallbackHook = [renderer] { renderer->replaceFramebuffer(); };
  DISPATCH_CHECK(!victim.graph->player_->dispatchNativeRestoreSeek());
  DISPATCH_CHECK(state->finalCallbackFired && state->loads == 1 && state->seeks == 0);
  victim.checkNoUnpause();
}

void TestHealthyGraphDispatch::targetAckRequiredBeforeSeek()
{
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  DISPATCH_CHECK(fixture.graph->restoreLoad()); fixture.replyLoad(42);
  fixture.observe(1, 42, "0.000"); fixture.qualifyBaseline();
  DISPATCH_CHECK(!fixture.graph->captureTargetReady());
  DISPATCH_CHECK(!fixture.graph->player_->dispatchNativeRestoreSeek());
  DISPATCH_CHECK(fixture.live->loads == 1 && fixture.live->seeks == 0);
  fixture.checkNoUnpause();
}

void TestHealthyGraphDispatch::staleTargetAckRejectsSeek()
{
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  DISPATCH_CHECK(fixture.graph->restoreLoad()); fixture.replyLoad(42);
  fixture.observe(1, 42, "0.000"); fixture.qualifyBaseline(); fixture.armTarget();
  fixture.renderer->replaceFramebuffer();
  DISPATCH_CHECK(!fixture.graph->captureTargetReady());
  DISPATCH_CHECK(!fixture.graph->player_->dispatchNativeRestoreSeek());
  DISPATCH_CHECK(fixture.live->loads == 1 && fixture.live->seeks == 0);
  fixture.checkNoUnpause();
}

void TestHealthyGraphDispatch::otherRendererAckCannotAuthorizeSeek()
{
  Fixture first; first.initialize(); first.acceptGrantAndPermit();
  DISPATCH_CHECK(first.graph->restoreLoad()); first.replyLoad(42);
  first.observe(1, 42, "0.000"); first.qualifyBaseline();
  Fixture other; other.initialize(); other.acceptGrantAndPermit();
  DISPATCH_CHECK(other.graph->restoreLoad()); other.replyLoad(42);
  other.observe(1, 42, "0.000"); other.qualifyBaseline(); other.armTarget();
  DISPATCH_CHECK(!first.graph->captureTargetReady());
  DISPATCH_CHECK(!first.graph->player_->dispatchNativeRestoreSeek());
  DISPATCH_CHECK(first.live->loads == 1 && first.live->seeks == 0 && other.live->seeks == 0);
  first.checkNoUnpause(); other.checkNoUnpause();
}

void TestHealthyGraphDispatch::cancelAfterTargetArmRejectsSeek()
{
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  DISPATCH_CHECK(fixture.graph->restoreLoad()); fixture.replyLoad(42);
  fixture.observe(1, 42, "0.000"); fixture.qualifyBaseline(); fixture.armTarget();
  QByteArray response;
  (void)fixture.graph->restoreControl(encodeControl({ControlKind::UserStop, 2, 5}), response);
  DISPATCH_CHECK(fixture.graph->session_->gate->canceled());
  DISPATCH_CHECK(!fixture.graph->player_->dispatchNativeRestoreSeek());
  DISPATCH_CHECK(fixture.live->loads == 1 && fixture.live->seeks == 0);
  fixture.checkNoUnpause();
}

void TestHealthyGraphDispatch::wrongOwnerRejectsDirectSeek()
{
  for (int mode = 0; mode != 5; ++mode) {
    Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
    DISPATCH_CHECK(fixture.graph->restoreLoad()); fixture.replyLoad(42);
    fixture.observe(1, 42, "0.000"); fixture.qualifyBaseline(); fixture.armTarget();
    auto& player = *fixture.graph->player_;
    if (mode == 0) ++player.m_nativeRestore.key.host;
    if (mode == 1) ++player.m_mpvGeneration;
    if (mode == 2) ++player.m_nativeRestore.key.revision;
    if (mode == 3) ++player.m_nativeRestore.key.attempt;
    if (mode == 4) player.m_playbackIntent.stop(PlaybackIntent::Reason::NativeStop);
    DISPATCH_CHECK(!player.dispatchNativeRestoreSeek());
    DISPATCH_CHECK(fixture.live->loads == 1 && fixture.live->seeks == 0);
    fixture.checkNoUnpause();
  }
}

void TestHealthyGraphDispatch::admittedSeekCannotRetry()
{
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  DISPATCH_CHECK(fixture.graph->restoreLoad()); fixture.replyLoad(42);
  fixture.observe(1, 42, "0.000"); fixture.qualifyBaseline(); fixture.armTarget();
  auto* player = fixture.graph->player_.get();
  const auto original = player->m_nativeRestore.key;
  fixture.live->submissionHook = [player] { player->requestNativeStop(); };
  (void)fixture.graph->restoreSeek();
  DISPATCH_CHECK(fixture.live->loads == 1 && fixture.live->seeks == 1);
  const auto& retained = player->m_nativeRestore;
  DISPATCH_CHECK(retained.seekAttempted && retained.seekSubmissionKnown && retained.seekAccepted &&
                 retained.key.host == original.host && retained.key.core == original.core &&
                 retained.key.revision == original.revision && retained.key.attempt == original.attempt);
  DISPATCH_CHECK(!player->m_restorePausedProof);
  DISPATCH_CHECK(!fixture.graph->restoreSeek() && fixture.live->seeks == 1);
  fixture.checkNoUnpause();
}

void TestHealthyGraphDispatch::desiredPausedCompletesHold()
{
  using Progress = FixtureGraph::PlaybackProgress;
  Fixture fixture; fixture.initialize(true); fixture.acceptGrantAndPermit();
  DISPATCH_CHECK(fixture.graph->restoreLoad()); fixture.replyLoad(42);
  fixture.observe(1, 42, "0.000"); fixture.qualifyBaseline(); fixture.armTarget();
  DISPATCH_CHECK(fixture.graph->restoreSeek());
  DISPATCH_CHECK(fixture.renderer->queue()); fixture.renderer->publish(90);
  fixture.completeSeekReplyAndRestart();
  DISPATCH_CHECK(fixture.drivePausedHold());
  DISPATCH_CHECK(fixture.graph->pollRestorePlayback() == Progress::Ready);
  DISPATCH_CHECK(fixture.graph->player_->m_nativeRestore.grant.desiredPaused);
  DISPATCH_CHECK(fixture.live->loads == 1 && fixture.live->seeks == 1 && fixture.live->pausedLoad);
  fixture.checkNoUnpause();
}

void TestHealthyGraphDispatch::wrongEntryCannotQualifyBaseline()
{
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  DISPATCH_CHECK(fixture.graph->restoreLoad()); fixture.replyLoad(42);
  fixture.observe(1, 43, "0.000");
  DISPATCH_CHECK(!fixture.graph->captureBaseline() && !fixture.graph->captureTarget());
  DISPATCH_CHECK(fixture.live->loads == 1 && fixture.live->seeks == 0);
  fixture.checkNoUnpause();
}

void TestHealthyGraphDispatch::preSeekTargetCannotQualify()
{
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  DISPATCH_CHECK(fixture.graph->restoreLoad()); fixture.replyLoad(42);
  fixture.observe(1, 42, "0.000"); fixture.qualifyBaseline(); fixture.armTarget();
  fixture.renderer->publish(90);
  DISPATCH_CHECK(fixture.graph->restoreSeek());
  fixture.completeSeekReplyAndRestart();
  DISPATCH_CHECK(!fixture.drivePausedHold());
  DISPATCH_CHECK(!fixture.graph->player_->m_restorePausedProof);
  DISPATCH_CHECK(fixture.live->loads == 1 && fixture.live->seeks == 1);
  fixture.checkNoUnpause();
}

void TestHealthyGraphDispatch::postSubmitTargetBeforeReplyCanQualify()
{
  using Progress = FixtureGraph::PlaybackProgress;
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  DISPATCH_CHECK(fixture.graph->restoreLoad()); fixture.replyLoad(42, true);
  fixture.observe(1, 42, "0.000"); fixture.qualifyBaseline(); fixture.armTarget();
  DISPATCH_CHECK(fixture.graph->restoreSeek());
  DISPATCH_CHECK(fixture.renderer->queue()); fixture.renderer->publish(90);
  DISPATCH_CHECK(fixture.graph->pollRestorePlayback() == Progress::Pending);
  DISPATCH_CHECK(!fixture.graph->player_->m_restorePausedProof);
  fixture.completeSeekReplyAndRestart();
  DISPATCH_CHECK(fixture.graph->pollRestorePlayback() == Progress::Pending);
  DISPATCH_CHECK(!fixture.graph->player_->m_restorePausedProof);
  DISPATCH_CHECK(fixture.drivePausedHold());
  DISPATCH_CHECK(fixture.graph->pollRestorePlayback() == Progress::Ready);
  DISPATCH_CHECK(fixture.graph->pollRestorePlayback() == Progress::Ready);
  DISPATCH_CHECK(fixture.live->loads == 1 && fixture.live->seeks == 1);
  fixture.checkNoUnpause();
}

void TestHealthyGraphDispatch::restartBeforeSeekReplyCannotQualify()
{
  using Progress = FixtureGraph::PlaybackProgress;
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  DISPATCH_CHECK(fixture.graph->restoreLoad()); fixture.replyLoad(42);
  fixture.observe(1, 42, "0.000"); fixture.qualifyBaseline(); fixture.armTarget();
  DISPATCH_CHECK(fixture.graph->restoreSeek());
  DISPATCH_CHECK(fixture.renderer->queue()); fixture.renderer->publish(90);
  mpv_event restart{}; restart.event_id = MPV_EVENT_PLAYBACK_RESTART;
  fixture.graph->player_->observeNativeReceipt(&restart);
  DISPATCH_CHECK(fixture.graph->pollRestorePlayback() == Progress::Pending);
  mpv_event reply{}; reply.event_id = MPV_EVENT_COMMAND_REPLY;
  reply.reply_userdata = (quint64{1} << 61) | fixture.graph->session_->attempt;
  fixture.graph->player_->observeNativeReceipt(&reply);
  DISPATCH_CHECK(fixture.graph->pollRestorePlayback() == Progress::Pending);
  DISPATCH_CHECK(!fixture.graph->player_->m_restorePausedProof);
  fixture.graph->player_->observeNativeReceipt(&restart);
  DISPATCH_CHECK(fixture.drivePausedHold());
  DISPATCH_CHECK(fixture.live->loads == 1 && fixture.live->seeks == 1);
  fixture.checkNoUnpause();
}

void TestHealthyGraphDispatch::earlyObservationCannotQualify()
{
  using Progress = FixtureGraph::PlaybackProgress;
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  DISPATCH_CHECK(fixture.graph->restoreLoad()); fixture.replyLoad(42);
  fixture.observe(1, 42, "0.000"); fixture.qualifyBaseline(); fixture.armTarget();
  DISPATCH_CHECK(fixture.graph->restoreSeek());
  DISPATCH_CHECK(fixture.renderer->queue()); fixture.renderer->publish(90);
  auto& player = *fixture.graph->player_;
  DISPATCH_CHECK(!player.requestNativeObservation(player.m_nativeReceipts.snapshot().key, 2, 10000, true));
  QByteArray text("NR1|1|42|yes|yes|3.000|yes|no|no");
  mpv_event_command command{}; command.result.format = MPV_FORMAT_STRING; command.result.u.string = text.data();
  mpv_event event{}; event.event_id = MPV_EVENT_COMMAND_REPLY;
  event.reply_userdata = (quint64{1} << 62) | 2; event.data = &command;
  player.observeNativeReceipt(&event);
  DISPATCH_CHECK(!player.m_nativeObservation.postSeek && !player.m_restorePausedProof);
  DISPATCH_CHECK(fixture.graph->pollRestorePlayback() == Progress::Pending);
  fixture.completeSeekReplyAndRestart();
  DISPATCH_CHECK(fixture.drivePausedHold());
  DISPATCH_CHECK(fixture.live->loads == 1 && fixture.live->seeks == 1);
  fixture.checkNoUnpause();
}

void TestHealthyGraphDispatch::runSeekCases()
{
#define RUN_DISPATCH_CASE(name) do { dispatchCase = #name; name(); } while (false)
  RUN_DISPATCH_CASE(permitThenLostEndpointRejectsDirectSeek);
  RUN_DISPATCH_CASE(revocationDuringSeekAuthorization);
  RUN_DISPATCH_CASE(clockInvalidatesTargetBeforeSeekAdmission);
  RUN_DISPATCH_CASE(lastLivenessCallbackInvalidatesTarget);
  RUN_DISPATCH_CASE(targetAckRequiredBeforeSeek);
  RUN_DISPATCH_CASE(staleTargetAckRejectsSeek);
  RUN_DISPATCH_CASE(otherRendererAckCannotAuthorizeSeek);
  RUN_DISPATCH_CASE(cancelAfterTargetArmRejectsSeek);
  RUN_DISPATCH_CASE(wrongOwnerRejectsDirectSeek);
  RUN_DISPATCH_CASE(admittedSeekCannotRetry);
  RUN_DISPATCH_CASE(wrongEntryCannotQualifyBaseline);
  RUN_DISPATCH_CASE(preSeekTargetCannotQualify);
  RUN_DISPATCH_CASE(postSubmitTargetBeforeReplyCanQualify);
  RUN_DISPATCH_CASE(restartBeforeSeekReplyCannotQualify);
  RUN_DISPATCH_CASE(earlyObservationCannotQualify);
  RUN_DISPATCH_CASE(desiredPausedCompletesHold);
#undef RUN_DISPATCH_CASE
}
