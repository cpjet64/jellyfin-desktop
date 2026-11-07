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
  const bool module = variant != 0;
  {
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

void TestHealthyGraphDispatch::sameGenerationDifferentClaimRejectsSeek()
{
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  DISPATCH_CHECK(fixture.graph->restoreLoad()); fixture.replyLoad(42);
  fixture.observe(1, 42, "0.000"); fixture.qualifyBaseline(); fixture.armTarget();
  auto original = std::atomic_load(&fixture.graph->access_->intentClaim);
  DISPATCH_CHECK(original && original->open());
  auto other = std::make_shared<RetirementClaim>(original->generation());
  DISPATCH_CHECK(other != original && other->open());
  std::atomic_store(&fixture.graph->access_->intentClaim, other);
  DISPATCH_CHECK(!fixture.graph->player_->dispatchNativeRestoreSeek());
  DISPATCH_CHECK(fixture.live->loads == 1 && fixture.live->seeks == 0);
  fixture.checkNoUnpause();
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
  Fixture victim; victim.initialize(); victim.acceptGrantAndPermit();
  DISPATCH_CHECK(victim.graph->restoreLoad()); victim.replyLoad(42);
  victim.observe(1, 42, "0.000"); victim.qualifyBaseline(); victim.armTarget();
  DISPATCH_CHECK(victim.graph->player_->healthyRestoreSubmissionAllowed(true));
  const auto claim = std::atomic_load(&victim.graph->access_->intentClaim);
  DISPATCH_CHECK(claim && claim->open() && !TestRetirementClaim::admitted(claim, true));
  auto state = victim.live; state->trace = true; state->triggerAt = 11;
  auto* renderer = victim.renderer.get();
  state->finalCallbackHook = [state, renderer] { DISPATCH_CHECK(traceBytes(state->callbacks) == expectedSeekTrace); renderer->replaceFramebuffer(); };
  DISPATCH_CHECK(!victim.graph->player_->dispatchNativeRestoreSeek());
  DISPATCH_CHECK(state->finalCallbackFired && state->loads == 1 && state->seeks == 0);
  DISPATCH_CHECK(!victim.graph->captureTargetReady());
  DISPATCH_CHECK(!TestRetirementClaim::admitted(claim, true));
  DISPATCH_CHECK(!victim.graph->player_->dispatchNativeRestoreSeek() && state->seeks == 0);
  victim.checkNoUnpause();
}

void TestHealthyGraphDispatch::lastLivenessCallbackClosesSeekAdmission()
{
  const int mode = variant;
  {
    Fixture victim; victim.initialize(); victim.acceptGrantAndPermit();
    DISPATCH_CHECK(victim.graph->restoreLoad()); victim.replyLoad(42);
    victim.observe(1, 42, "0.000"); victim.qualifyBaseline(); victim.armTarget();
    DISPATCH_CHECK(victim.graph->player_->healthyRestoreSubmissionAllowed(true));
    auto state = victim.live; state->trace = true; state->triggerAt = mode == 0 ? 11 : 7;
    const auto claim = std::atomic_load(&victim.graph->access_->intentClaim);
    DISPATCH_CHECK(claim && claim->open());
    DISPATCH_CHECK(!TestRetirementClaim::admitted(claim, true));
    const auto deadline = victim.graph->normalDeadline_;
    state->finalCallbackHook = [state, claim, deadline, mode] {
      DISPATCH_CHECK(traceBytes(state->callbacks) == (mode == 0 ? expectedSeekTrace : "CCMEMEC"));
      if (mode == 0) claim->revoke(); else state->clockOffset = deadline - Fixture::now();
    };
    DISPATCH_CHECK(!victim.graph->player_->dispatchNativeRestoreSeek());
    DISPATCH_CHECK(state->finalCallbackFired && state->loads == 1 && state->seeks == 0);
    DISPATCH_CHECK(mode == 0 ? !claim->open() : state->clockNs.load() >= deadline);
    DISPATCH_CHECK(!TestRetirementClaim::admitted(claim, true));
    DISPATCH_CHECK(!victim.graph->player_->dispatchNativeRestoreSeek() && state->seeks == 0);
    victim.checkNoUnpause();
  }
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
  const auto key = first.graph->player_->m_nativeRestore.key;
  const auto claim = std::atomic_load(&first.graph->access_->intentClaim);
  DISPATCH_CHECK(claim && claim->open() && first.graph->player_->healthyRestoreSubmissionAllowed(false));
  DISPATCH_CHECK(first.graph->player_->restoreOwnsCurrent() && first.graph->captureTargetRequested_);
  DISPATCH_CHECK(first.graph->player_->nativeObservationCurrent() && first.graph->player_->m_nativeObservation.paused);
  const auto otherClaim = TestRetirementClaim::reserve(9, {key.host, key.core, key.revision, key.attempt});
  auto otherSlot = std::make_shared<Slot>(9, first.graph->deadline_, Fixture::now);
  auto otherController = CaptureController::create(otherSlot);
  DISPATCH_CHECK(otherController);
  auto revoked = std::make_shared<std::atomic_bool>(false);
  DISPATCH_CHECK(otherSlot->bindModuleForTest([] { return true; }, revoked));
  int otherContext = 0;
  DISPATCH_CHECK(otherSlot->beginCreation() && otherSlot->beforeCreate() && otherSlot->recordCreated(&otherContext));
  HealthyRetirement::Operations ops;
  ops.currentIdentity = [] { return Identity{11, 12, 13}; };
  ops.unregisterCallback = [](void*) {};
  ops.invalidateQueued = [](std::uint64_t) { return true; };
  ops.freeContext = [](void*) {};
  ops.clearPointer = [](void*) { return true; };
  DISPATCH_CHECK(otherSlot->install({11, 12, 13}, &otherContext, ops));
  DISPATCH_CHECK(otherSlot->bindStopClaimForTest(otherClaim));
  TestCaptureCapability other(otherSlot); DISPATCH_CHECK(other.initialize());
  DISPATCH_CHECK(other.acknowledgeIndependentTarget({first.graph->session_->launch, key.core, 13, 42, 1, 0},
                                                     key.attempt, otherClaim));
  DISPATCH_CHECK(!first.graph->captureTargetReady());
  DISPATCH_CHECK(!first.graph->player_->dispatchNativeRestoreSeek());
  DISPATCH_CHECK(first.live->loads == 1 && first.live->seeks == 0 && claim->open());
  first.checkNoUnpause();
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
  const int mode = variant;
  {
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

void TestHealthyGraphDispatch::retainedHoldCannotOutliveTargetQualification()
{
  using Progress = FixtureGraph::PlaybackProgress;
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  DISPATCH_CHECK(fixture.graph->restoreLoad()); fixture.replyLoad(42);
  fixture.observe(1, 42, "0.000"); fixture.qualifyBaseline(); fixture.armTarget();
  DISPATCH_CHECK(fixture.graph->restoreSeek());
  DISPATCH_CHECK(fixture.renderer->queue()); fixture.renderer->publish(90);
  fixture.completeSeekReplyAndRestart();
  DISPATCH_CHECK(fixture.drivePausedHold() && fixture.graph->player_->m_restorePausedProof);
  fixture.renderer->replaceFramebuffer();
  DISPATCH_CHECK(fixture.graph->pollRestorePlayback() != Progress::Ready);
  DISPATCH_CHECK(fixture.live->loads == 1 && fixture.live->seeks == 1);
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
  RUN_DISPATCH_CASE(sameGenerationDifferentClaimRejectsSeek);
  RUN_DISPATCH_CASE(clockInvalidatesTargetBeforeSeekAdmission);
  RUN_DISPATCH_CASE(lastLivenessCallbackInvalidatesTarget);
  RUN_DISPATCH_CASE(lastLivenessCallbackClosesSeekAdmission);
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
  RUN_DISPATCH_CASE(retainedHoldCannotOutliveTargetQualification);
#undef RUN_DISPATCH_CASE
}
