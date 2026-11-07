#include "test_healthy_graph_dispatch_fixture.h"

void TestHealthyGraphDispatch::bothGrantsLoadPaused()
{
  const bool desiredPaused = variant != 0;
  {
    Fixture fixture; fixture.initialize(desiredPaused); fixture.acceptGrantAndPermit();
    DISPATCH_CHECK(fixture.graph->restoreLoad());
    DISPATCH_CHECK(fixture.live->loads == 1 && fixture.live->seeks == 0 && fixture.live->pausedLoad);
    DISPATCH_CHECK(!fixture.graph->restoreLoad() && fixture.live->loads == 1);
    fixture.checkNoUnpause();
  }
}

void TestHealthyGraphDispatch::permitThenLostEndpointRejectsDirectLoad()
{
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  auto& player = *fixture.graph->player_;
  DISPATCH_CHECK(player.m_healthyRestoreAuthority);
  fixture.live->endpoints = false;
  DISPATCH_CHECK(!player.dispatchNativeRestoreLoad(fixture.media->fileName()));
  DISPATCH_CHECK(fixture.live->loads == 0 && fixture.live->seeks == 0);
  fixture.checkNoUnpause();
}

void TestHealthyGraphDispatch::permitThenLostEndpointRejectsReceiptEntry()
{
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  auto& player = *fixture.graph->player_;
  DISPATCH_CHECK(player.m_healthyRestoreAuthority);
  const auto key = player.m_nativeRestore.key;
  fixture.live->endpoints = false;
  DISPATCH_CHECK(!player.beginNativeReceipt(PlayerComponent::FixtureAsset::TestClip,
                                           fixture.media->fileName(), key.host, key.attempt, true));
  DISPATCH_CHECK(fixture.live->loads == 0 && fixture.live->seeks == 0);
  fixture.checkNoUnpause();
}

void TestHealthyGraphDispatch::moduleRevocationBeforeLoadAdmission()
{
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  auto state = fixture.live;
  state->moduleHook = [state] { state->module = false; };
  DISPATCH_CHECK(!fixture.graph->player_->dispatchNativeRestoreLoad(fixture.media->fileName()));
  DISPATCH_CHECK(state->loads == 0 && state->seeks == 0);
  fixture.checkNoUnpause();
}

void TestHealthyGraphDispatch::endpointRevocationBeforeLoadAdmission()
{
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  auto state = fixture.live;
  state->endpointHook = [state] { state->endpoints = false; };
  DISPATCH_CHECK(!fixture.graph->player_->dispatchNativeRestoreLoad(fixture.media->fileName()));
  DISPATCH_CHECK(state->loads == 0 && state->seeks == 0);
  fixture.checkNoUnpause();
}

void TestHealthyGraphDispatch::claimRevocationBeforeLoadAdmission()
{
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  const auto claim = std::atomic_load(&fixture.graph->access_->intentClaim);
  DISPATCH_CHECK(claim && claim->open());
  fixture.live->moduleHook = [claim] { claim->revoke(); };
  DISPATCH_CHECK(!fixture.graph->player_->dispatchNativeRestoreLoad(fixture.media->fileName()));
  DISPATCH_CHECK(fixture.live->loads == 0 && fixture.live->seeks == 0);
  fixture.checkNoUnpause();
}

void TestHealthyGraphDispatch::clockCancellationBeforeLoadAdmission()
{
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  auto* graph = fixture.graph;
  fixture.live->clockHook = [graph] {
    QByteArray response;
    (void)graph->restoreControl(encodeControl({ControlKind::UserStop, 2, 5}), response);
  };
  DISPATCH_CHECK(!graph->player_->dispatchNativeRestoreLoad(fixture.media->fileName()));
  DISPATCH_CHECK(graph->session_->gate->canceled());
  DISPATCH_CHECK(fixture.live->loads == 0 && fixture.live->seeks == 0);
  fixture.checkNoUnpause();
}

void TestHealthyGraphDispatch::clockRevokesEndpointBeforeLoadAdmission()
{
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  auto state = fixture.live; bool fired = false;
  state->clockHook = [state, &fired] { fired = true; state->endpoints = false; };
  DISPATCH_CHECK(!fixture.graph->player_->dispatchNativeRestoreLoad(fixture.media->fileName()));
  DISPATCH_CHECK(fired && state->loads == 0 && state->seeks == 0);
  fixture.checkNoUnpause();
}

void TestHealthyGraphDispatch::lastLivenessCallbackRevokesLoadAuthority()
{
  std::vector<char> callbacks;
  {
    Fixture control; control.initialize(); control.acceptGrantAndPermit();
    control.live->trace = true;
    DISPATCH_CHECK(control.graph->player_->dispatchNativeRestoreLoad(control.media->fileName()));
    callbacks = control.live->callbacksAtSubmission;
    DISPATCH_CHECK(control.live->loads == 1 && !callbacks.empty());
  }
  const auto last = callbacks.back();
  DISPATCH_CHECK(last == 'M' || last == 'E');
  DISPATCH_CHECK(std::find(callbacks.begin(), callbacks.end(), 'C') != callbacks.end());
  Fixture victim; victim.initialize(); victim.acceptGrantAndPermit();
  auto state = victim.live; state->trace = true; state->triggerAt = callbacks.size();
  state->finalCallbackHook = [state, last] {
    if (last == 'M') state->endpoints = false; else state->module = false;
  };
  DISPATCH_CHECK(!victim.graph->player_->dispatchNativeRestoreLoad(victim.media->fileName()));
  DISPATCH_CHECK(state->finalCallbackFired && state->loads == 0 && state->seeks == 0);
  victim.checkNoUnpause();
}

void TestHealthyGraphDispatch::lastLivenessCallbackRevokesClaimBeforeLoad()
{
  std::vector<char> callbacks;
  {
    Fixture control; control.initialize(); control.acceptGrantAndPermit(); control.live->trace = true;
    DISPATCH_CHECK(control.graph->player_->dispatchNativeRestoreLoad(control.media->fileName()));
    callbacks = control.live->callbacksAtSubmission;
    DISPATCH_CHECK(control.live->loads == 1 && !callbacks.empty());
  }
  DISPATCH_CHECK(callbacks.back() == 'M' || callbacks.back() == 'E');
  Fixture victim; victim.initialize(); victim.acceptGrantAndPermit();
  auto state = victim.live; state->trace = true; state->triggerAt = callbacks.size();
  const auto claim = std::atomic_load(&victim.graph->access_->intentClaim);
  DISPATCH_CHECK(claim && claim->open());
  state->finalCallbackHook = [claim] { claim->revoke(); };
  DISPATCH_CHECK(!victim.graph->player_->dispatchNativeRestoreLoad(victim.media->fileName()));
  DISPATCH_CHECK(state->finalCallbackFired && state->loads == 0 && state->seeks == 0);
  victim.checkNoUnpause();
}

void TestHealthyGraphDispatch::lastLivenessCallbackExpiresLoadDeadline()
{
  std::vector<char> callbacks;
  {
    Fixture control; control.initialize(); control.acceptGrantAndPermit(); control.live->trace = true;
    DISPATCH_CHECK(control.graph->player_->dispatchNativeRestoreLoad(control.media->fileName()));
    callbacks = control.live->callbacksAtSubmission;
    DISPATCH_CHECK(control.live->loads == 1 && !callbacks.empty());
  }
  DISPATCH_CHECK(callbacks.back() == 'M' || callbacks.back() == 'E');
  Fixture victim; victim.initialize(); victim.acceptGrantAndPermit();
  auto state = victim.live; state->trace = true; state->triggerAt = callbacks.size();
  const auto deadline = victim.graph->normalDeadline_;
  state->finalCallbackHook = [state, deadline] { state->clockNs = deadline; };
  DISPATCH_CHECK(!victim.graph->player_->dispatchNativeRestoreLoad(victim.media->fileName()));
  DISPATCH_CHECK(state->finalCallbackFired && state->loads == 0 && state->seeks == 0);
  victim.checkNoUnpause();
}

void TestHealthyGraphDispatch::admittedLoadCannotRetry()
{
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  auto* player = fixture.graph->player_.get();
  const auto original = player->m_nativeRestore.key;
  fixture.live->submissionHook = [player] { player->requestNativeStop(); };
  (void)fixture.graph->restoreLoad();
  DISPATCH_CHECK(fixture.live->loads == 1 && fixture.live->seeks == 0);
  const auto receipt = player->m_nativeReceipts.snapshot();
  DISPATCH_CHECK(receipt.submissionObserved && receipt.submitted && receipt.key.host == original.host &&
                 receipt.key.core == original.core && receipt.key.revision == original.revision &&
                 receipt.key.attempt == original.attempt);
  DISPATCH_CHECK(!fixture.graph->restoreLoad() && fixture.live->loads == 1);
  fixture.checkNoUnpause();
}

void TestHealthyGraphDispatch::cleanupRetainsOriginalOwnerAfterLoad()
{
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  DISPATCH_CHECK(fixture.graph->restoreLoad());
  auto& player = *fixture.graph->player_;
  const auto original = player.m_nativeRestore.key;
  DISPATCH_CHECK(fixture.prepared->beginRestoreFailure(HealthyRestart::FailureCause::ProtocolViolation));
  const auto cleanup = player.m_restoreCleanup.key;
  DISPATCH_CHECK(cleanup.host == original.host && cleanup.core == original.core &&
                 cleanup.revision == original.revision && cleanup.attempt == original.attempt);
  DISPATCH_CHECK(!fixture.graph->restoreLoad() && !fixture.graph->restoreSeek());
  DISPATCH_CHECK(fixture.live->loads == 1 && fixture.live->seeks == 0);
  fixture.checkNoUnpause();
}

void TestHealthyGraphDispatch::wrongOwnerRejectsDirectLoad()
{
  const int mode = variant;
  {
    Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
    auto& player = *fixture.graph->player_;
    if (mode == 0) ++player.m_nativeRestore.key.host;
    if (mode == 1) ++player.m_mpvGeneration;
    if (mode == 2) ++player.m_nativeRestore.key.revision;
    if (mode == 3) ++player.m_nativeRestore.key.attempt;
    if (mode == 4) player.m_playbackIntent.stop(PlaybackIntent::Reason::NativeStop);
    DISPATCH_CHECK(!player.dispatchNativeRestoreLoad(fixture.media->fileName()));
    DISPATCH_CHECK(fixture.live->loads == 0 && fixture.live->seeks == 0);
    fixture.checkNoUnpause();
  }
}

void TestHealthyGraphDispatch::replyWithoutStartCannotCapture()
{
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  DISPATCH_CHECK(fixture.graph->restoreLoad()); fixture.replyLoad(42, false, false);
  DISPATCH_CHECK(!fixture.graph->captureBaseline() && !fixture.graph->captureTarget());
  DISPATCH_CHECK(!fixture.graph->restoreSeek());
  DISPATCH_CHECK(fixture.live->loads == 1 && fixture.live->seeks == 0);
  fixture.checkNoUnpause();
}

void TestHealthyGraphDispatch::originalDeadlineRejectsLateLoad()
{
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  fixture.live->clockOffset = 20000000000LL;
  DISPATCH_CHECK(!fixture.graph->player_->dispatchNativeRestoreLoad(fixture.media->fileName()));
  DISPATCH_CHECK(fixture.live->loads == 0 && fixture.live->seeks == 0);
  fixture.checkNoUnpause();
}

void TestHealthyGraphDispatch::pendingLoadExpiresWithoutSuccess()
{
  using Progress = FixtureGraph::PlaybackProgress;
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  DISPATCH_CHECK(fixture.graph->restoreLoad());
  DISPATCH_CHECK(fixture.graph->pollRestorePlayback() == Progress::Pending);
  fixture.live->clockOffset = 20000000000LL;
  DISPATCH_CHECK(fixture.graph->pollRestorePlayback() == Progress::Failed);
  DISPATCH_CHECK(!fixture.graph->player_->m_restorePausedProof);
  DISPATCH_CHECK(fixture.live->loads == 1 && fixture.live->seeks == 0);
  fixture.checkNoUnpause();
}

void TestHealthyGraphDispatch::runLoadCases()
{
#define RUN_DISPATCH_CASE(name) do { dispatchCase = #name; name(); } while (false)
  RUN_DISPATCH_CASE(bothGrantsLoadPaused);
  RUN_DISPATCH_CASE(permitThenLostEndpointRejectsDirectLoad);
  RUN_DISPATCH_CASE(permitThenLostEndpointRejectsReceiptEntry);
  RUN_DISPATCH_CASE(moduleRevocationBeforeLoadAdmission);
  RUN_DISPATCH_CASE(endpointRevocationBeforeLoadAdmission);
  RUN_DISPATCH_CASE(claimRevocationBeforeLoadAdmission);
  RUN_DISPATCH_CASE(clockCancellationBeforeLoadAdmission);
  RUN_DISPATCH_CASE(clockRevokesEndpointBeforeLoadAdmission);
  RUN_DISPATCH_CASE(lastLivenessCallbackRevokesLoadAuthority);
  RUN_DISPATCH_CASE(lastLivenessCallbackRevokesClaimBeforeLoad);
  RUN_DISPATCH_CASE(lastLivenessCallbackExpiresLoadDeadline);
  RUN_DISPATCH_CASE(admittedLoadCannotRetry);
  RUN_DISPATCH_CASE(cleanupRetainsOriginalOwnerAfterLoad);
  RUN_DISPATCH_CASE(wrongOwnerRejectsDirectLoad);
  RUN_DISPATCH_CASE(replyWithoutStartCannotCapture);
  RUN_DISPATCH_CASE(originalDeadlineRejectsLateLoad);
  RUN_DISPATCH_CASE(pendingLoadExpiresWithoutSuccess);
  RUN_DISPATCH_CASE(bootstrapRejectsMalformed);
#undef RUN_DISPATCH_CASE
}
