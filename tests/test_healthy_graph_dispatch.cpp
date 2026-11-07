#include "test_healthy_graph_dispatch_fixture.h"

int TestHealthyGraphDispatch::variant = 0;

bool TestHealthyGraphDispatch::runSelected(const QByteArray& selected)
{
#define RUN_CASE(name) if (selected == #name) { dispatchCase = #name; name(); return true; }
  if (selected == "bothGrantsLoadPaused_0") { variant = 0; dispatchCase = "bothGrantsLoadPaused_0"; bothGrantsLoadPaused(); return true; }
  if (selected == "bothGrantsLoadPaused_1") { variant = 1; dispatchCase = "bothGrantsLoadPaused_1"; bothGrantsLoadPaused(); return true; }
  RUN_CASE(permitThenLostEndpointRejectsDirectLoad)
  RUN_CASE(permitThenLostEndpointRejectsReceiptEntry)
  RUN_CASE(moduleRevocationBeforeLoadAdmission)
  RUN_CASE(endpointRevocationBeforeLoadAdmission)
  RUN_CASE(claimRevocationBeforeLoadAdmission)
  RUN_CASE(clockCancellationBeforeLoadAdmission)
  RUN_CASE(clockRevokesEndpointBeforeLoadAdmission)
  RUN_CASE(admittedLoadCannotRetry)
  RUN_CASE(cleanupRetainsOriginalOwnerAfterLoad)
  if (selected == "wrongOwnerRejectsDirectLoad_0") { variant = 0; dispatchCase = "wrongOwnerRejectsDirectLoad_0"; wrongOwnerRejectsDirectLoad(); return true; }
  if (selected == "wrongOwnerRejectsDirectLoad_1") { variant = 1; dispatchCase = "wrongOwnerRejectsDirectLoad_1"; wrongOwnerRejectsDirectLoad(); return true; }
  if (selected == "wrongOwnerRejectsDirectLoad_2") { variant = 2; dispatchCase = "wrongOwnerRejectsDirectLoad_2"; wrongOwnerRejectsDirectLoad(); return true; }
  if (selected == "wrongOwnerRejectsDirectLoad_3") { variant = 3; dispatchCase = "wrongOwnerRejectsDirectLoad_3"; wrongOwnerRejectsDirectLoad(); return true; }
  if (selected == "wrongOwnerRejectsDirectLoad_4") { variant = 4; dispatchCase = "wrongOwnerRejectsDirectLoad_4"; wrongOwnerRejectsDirectLoad(); return true; }
  RUN_CASE(replyWithoutStartCannotCapture)
  RUN_CASE(originalDeadlineRejectsLateLoad)
  RUN_CASE(pendingLoadExpiresWithoutSuccess)
  RUN_CASE(bootstrapRejectsMalformed)
  RUN_CASE(permitThenLostEndpointRejectsDirectSeek)
  if (selected == "revocationDuringSeekAuthorization_0") { variant = 0; dispatchCase = "revocationDuringSeekAuthorization_0"; revocationDuringSeekAuthorization(); return true; }
  if (selected == "revocationDuringSeekAuthorization_1") { variant = 1; dispatchCase = "revocationDuringSeekAuthorization_1"; revocationDuringSeekAuthorization(); return true; }
  RUN_CASE(sameGenerationDifferentClaimRejectsSeek)
  RUN_CASE(clockInvalidatesTargetBeforeSeekAdmission)
  RUN_CASE(targetAckRequiredBeforeSeek)
  RUN_CASE(staleTargetAckRejectsSeek)
  RUN_CASE(cancelAfterTargetArmRejectsSeek)
  if (selected == "wrongOwnerRejectsDirectSeek_0") { variant = 0; dispatchCase = "wrongOwnerRejectsDirectSeek_0"; wrongOwnerRejectsDirectSeek(); return true; }
  if (selected == "wrongOwnerRejectsDirectSeek_1") { variant = 1; dispatchCase = "wrongOwnerRejectsDirectSeek_1"; wrongOwnerRejectsDirectSeek(); return true; }
  if (selected == "wrongOwnerRejectsDirectSeek_2") { variant = 2; dispatchCase = "wrongOwnerRejectsDirectSeek_2"; wrongOwnerRejectsDirectSeek(); return true; }
  if (selected == "wrongOwnerRejectsDirectSeek_3") { variant = 3; dispatchCase = "wrongOwnerRejectsDirectSeek_3"; wrongOwnerRejectsDirectSeek(); return true; }
  if (selected == "wrongOwnerRejectsDirectSeek_4") { variant = 4; dispatchCase = "wrongOwnerRejectsDirectSeek_4"; wrongOwnerRejectsDirectSeek(); return true; }
  RUN_CASE(admittedSeekCannotRetry)
  RUN_CASE(wrongEntryCannotQualifyBaseline)
  RUN_CASE(preSeekTargetCannotQualify)
  RUN_CASE(postSubmitTargetBeforeReplyCanQualify)
  RUN_CASE(restartBeforeSeekReplyCannotQualify)
  RUN_CASE(earlyObservationCannotQualify)
  RUN_CASE(desiredPausedCompletesHold)
  RUN_CASE(retainedHoldCannotOutliveTargetQualification)
#undef RUN_CASE
  return false;
}


void TestHealthyGraphDispatch::traceLoad()
{
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  fixture.live->trace = true;
  DISPATCH_CHECK(fixture.graph->player_->dispatchNativeRestoreLoad(fixture.media->fileName()));
  const auto& callbacks = fixture.live->callbacksAtSubmission;
  DISPATCH_CHECK(fixture.live->loads == 1 && !callbacks.empty());
  std::fprintf(stderr, "load trace: %.*s\n", int(callbacks.size()), callbacks.data());
}

void TestHealthyGraphDispatch::traceSeek()
{
  Fixture fixture; fixture.initialize(); fixture.acceptGrantAndPermit();
  DISPATCH_CHECK(fixture.graph->restoreLoad()); fixture.replyLoad(42);
  fixture.observe(1, 42, "0.000"); fixture.qualifyBaseline(); fixture.armTarget();
  fixture.live->trace = true;
  DISPATCH_CHECK(fixture.graph->player_->dispatchNativeRestoreSeek());
  const auto& callbacks = fixture.live->callbacksAtSubmission;
  DISPATCH_CHECK(fixture.live->seeks == 1 && !callbacks.empty());
  std::fprintf(stderr, "seek trace: %.*s\n", int(callbacks.size()), callbacks.data());
}

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  if (argc == 2 && QByteArray(argv[1]) == "trace-load") { TestHealthyGraphDispatch::traceLoad(); return 0; }
  if (argc == 2 && QByteArray(argv[1]) == "trace-seek") { TestHealthyGraphDispatch::traceSeek(); return 0; }
  if (argc == 2 && TestHealthyGraphDispatch::runSelected(argv[1])) return 0;
  const QByteArray selected = argc == 2 ? QByteArray(argv[1]) : QByteArray();
  if (selected.startsWith("lastLivenessCallback") || selected == "otherRendererAckCannotAuthorizeSeek")
    std::fprintf(stderr, "Held Graph dispatch selector: %s\n", selected.constData());
  else std::fprintf(stderr, "Unknown Graph dispatch selector\n");
  return 2;
}
