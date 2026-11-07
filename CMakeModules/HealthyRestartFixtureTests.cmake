# Test-only fixture sources share the same copied Graph and Player headers as the overlay.
function(healthy_restart_add_restore_fixture target)
  target_sources(${target} PRIVATE
    "${_healthy_copy}/mpvqt/restore_test_fixture.cpp"
    "${_healthy_copy}/mpvqt/restore_test_events.cpp"
    "${_healthy_copy}/mpvqt/restore_test_renderer.cpp")
endfunction()

function(healthy_restart_cleanup_targets)
  set(definitions HEALTHY_RESTART_DIAGNOSTIC HEALTHY_RETIREMENT_TESTING
    HEALTHY_MODULE_TESTING HEALTHY_ENDPOINT_TESTING)
  add_executable(healthy_healthy_restore_cancel test_healthy_restore_cancel.cpp)
  target_compile_definitions(healthy_healthy_restore_cancel PRIVATE ${definitions})
  target_link_libraries(healthy_healthy_restore_cancel PRIVATE HealthyGraphTestCore Qt6::Test)
  healthy_restart_add_restore_fixture(healthy_healthy_restore_cancel)
  healthy_restart_cancel_cases()
  add_executable(healthy_player_restore_cleanup test_healthy_player_restore_cleanup.cpp)
  target_compile_definitions(healthy_player_restore_cleanup PRIVATE HEALTHY_COPIED_SOURCE_TEST ${definitions})
  target_link_libraries(healthy_player_restore_cleanup PRIVATE HealthyGraphTestCore Qt6::Test)
  healthy_restart_add_restore_fixture(healthy_player_restore_cleanup)
  healthy_restart_cleanup_cases()
endfunction()

function(healthy_restart_observation_cases)
  foreach(case IN ITEMS barrierAndStaleReply failedSeekAndReplacement diagnosticStopReplacement
      diagnosticLoadReplacement diagnosticCoreReplacement ordinaryObservation)
    add_test(NAME healthy_player_restore_observation.${case} COMMAND healthy_player_restore_observation ${case})
    set_tests_properties(healthy_player_restore_observation.${case} PROPERTIES TIMEOUT 60)
  endforeach()
endfunction()

function(healthy_restart_native_restore_cases)
  foreach(case IN ITEMS actualObservationBoundary lateAndDuplicateObservation pausedLoadAndExactSeek
      cancellationBeforeLoad staleObservationAndGrant cancellationAfterLoad fixtureBridgeControl
      fixtureBridgeCapture lateAfterEndAndCoreChange malformedAndUnrelatedReplies
      reorderedLoadAndNativeConsumption cancellationDuringSeek reentrantCancellationAndSeekFailure
      diagnosticPosition1000Rejected diagnosticPosition3125Rejected diagnosticReentrantObservation)
    add_test(NAME healthy_player_native_restore.${case} COMMAND healthy_player_native_restore ${case})
    set_tests_properties(healthy_player_native_restore.${case} PROPERTIES TIMEOUT 60)
  endforeach()
endfunction()

function(healthy_restart_capture_cases)
  foreach(case IN ITEMS uninitializedGraphRejectsCapture positiveHandoff wrongBindingRejected
      wrongLaunchBindingRejected framebufferReplacementRevokesBaseline revokedClaimRejectsCapture
      cleanupRetainsAccessButClosesCapture rendererDestructionRejectsCapture badBaselineRejected-0
      badBaselineRejected-1 badBaselineRejected-2 replayAndContextRejected changedEntryRejected
      lateDeliveryAfterContainment slotInvalidatedBeforeRendererIssue slotInvalidatesQueuedTicket
      failedClaimInstallationCannotCapture baselinePixelsCannotQualifyTarget targetPixelsBeforeTargetArmRejected
      sameIdentityRejectsOldRendererGeneration)
    add_test(NAME healthy_graph_capture.${case} COMMAND healthy_graph_capture ${case})
    set_tests_properties(healthy_graph_capture.${case} PROPERTIES TIMEOUT 60)
  endforeach()
endfunction()

function(healthy_restart_unpause_cases)
  foreach(case IN ITEMS actualPausedHoldAndUnpause missingPermitAndImage:missing-permit
      missingPermitAndImage:missing-image)
    add_test(NAME healthy_unpause_player.${case} COMMAND healthy_unpause_player ${case})
    set_tests_properties(healthy_unpause_player.${case} PROPERTIES TIMEOUT 60)
  endforeach()
endfunction()

function(healthy_restart_cancel_cases)
  foreach(case IN ITEMS beforeLoad canceledGateCannotBind noLoadRejectsUnsolicitedEvents:start
      noLoadRejectsUnsolicitedEvents:end noLoadRejectsUnsolicitedEvents:reply reentrantQueriesRequireLaterDrain
      submittedWithoutStart bufferedStartBeforeReply lateEntryDuringTerminalQuery:end lateEntryDuringTerminalQuery:start
      terminalOrdering:reply-first terminalOrdering:end-first rejectedAndWrongReplies:rejected
      rejectedAndWrongReplies:wrong-reply pendingSeekBlocksSettlement duplicateAndReentrant
      supersededAndEventLoss:intent supersededAndEventLoss:core supersededAndEventLoss:event-loss
      missingIdentityAndRedirect:missing missingIdentityAndRedirect:redirect)
    add_test(NAME healthy_healthy_restore_cancel.${case} COMMAND healthy_healthy_restore_cancel ${case})
    set_tests_properties(healthy_healthy_restore_cancel.${case} PROPERTIES TIMEOUT 60)
  endforeach()
endfunction()

function(healthy_restart_cleanup_cases)
  foreach(case IN ITEMS newerLoadAndStop:newer-load newerLoadAndStop:newer-stop independentPlayerIsolation
      staleCoreAndHost:core staleCoreAndHost:host staleCoreAndHost:exiting seekSubmissionFailureCleanup
      asyncFailureAndReentrancy newerIntentAfterFailure bindingMustPrecedeArm:before bindingMustPrecedeArm:after
      bindingMustPrecedeArm:reentrant queuedStopReplyAfterContainment pendingLoadSettlesBeforeStop
      pendingLoadExpiresBeforeStop containedPendingLoadContinuesOnlyForCurrentOwner:current
      containedPendingLoadContinuesOnlyForCurrentOwner:deadline containedPendingLoadContinuesOnlyForCurrentOwner:new-core
      containedPendingLoadWithoutWakeExpires)
    add_test(NAME healthy_player_restore_cleanup.${case} COMMAND healthy_player_restore_cleanup ${case})
    set_tests_properties(healthy_player_restore_cleanup.${case} PROPERTIES TIMEOUT 60)
  endforeach()
endfunction()

function(healthy_restart_dispatch_cases)
  foreach(case IN ITEMS
    permitThenLostEndpointRejectsDirectLoad
    permitThenLostEndpointRejectsReceiptEntry
    moduleRevocationBeforeLoadAdmission
    endpointRevocationBeforeLoadAdmission
    claimRevocationBeforeLoadAdmission
    clockCancellationBeforeLoadAdmission
    clockRevokesEndpointBeforeLoadAdmission
    admittedLoadCannotRetry
    cleanupRetainsOriginalOwnerAfterLoad
    replyWithoutStartCannotCapture
    originalDeadlineRejectsLateLoad
    pendingLoadExpiresWithoutSuccess
    bootstrapRejectsMalformed
    permitThenLostEndpointRejectsDirectSeek
    sameGenerationDifferentClaimRejectsSeek
    clockInvalidatesTargetBeforeSeekAdmission
    targetAckRequiredBeforeSeek
    staleTargetAckRejectsSeek
    otherRendererAckCannotAuthorizeSeek
    cancelAfterTargetArmRejectsSeek
    admittedSeekCannotRetry
    wrongEntryCannotQualifyBaseline
    preSeekTargetCannotQualify
    postSubmitTargetBeforeReplyCanQualify
    restartBeforeSeekReplyCannotQualify
    earlyObservationCannotQualify
    desiredPausedCompletesHold
    retainedHoldCannotOutliveTargetQualification
    bothGrantsLoadPaused_0
    bothGrantsLoadPaused_1
    wrongOwnerRejectsDirectLoad_0
    wrongOwnerRejectsDirectLoad_1
    wrongOwnerRejectsDirectLoad_2
    wrongOwnerRejectsDirectLoad_3
    wrongOwnerRejectsDirectLoad_4
    revocationDuringSeekAuthorization_0
    revocationDuringSeekAuthorization_1
    wrongOwnerRejectsDirectSeek_0
    wrongOwnerRejectsDirectSeek_1
    wrongOwnerRejectsDirectSeek_2
    wrongOwnerRejectsDirectSeek_3
    wrongOwnerRejectsDirectSeek_4
    lastLivenessCallbackRevokesLoadAuthority
    lastLivenessCallbackRevokesClaimBeforeLoad
    lastLivenessCallbackExpiresLoadDeadline
    lastLivenessCallbackInvalidatesTarget
    lastLivenessCallbackClosesSeekAdmission
    trace-load trace-seek)
    add_test(NAME healthy_graph_dispatch.${case} COMMAND healthy_graph_dispatch ${case})
    set_tests_properties(healthy_graph_dispatch.${case} PROPERTIES TIMEOUT 60)
  endforeach()
endfunction()
