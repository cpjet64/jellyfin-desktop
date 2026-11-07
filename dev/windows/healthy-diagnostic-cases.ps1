function Get-HealthyCleanupCases {
    param([string]$Target)
    if ($Target -eq 'healthy_healthy_restore_cancel') {
        return @('beforeLoad', 'canceledGateCannotBind', 'noLoadRejectsUnsolicitedEvents:start',
            'noLoadRejectsUnsolicitedEvents:end', 'noLoadRejectsUnsolicitedEvents:reply', 'reentrantQueriesRequireLaterDrain',
            'submittedWithoutStart', 'bufferedStartBeforeReply', 'lateEntryDuringTerminalQuery:end',
            'lateEntryDuringTerminalQuery:start', 'terminalOrdering:reply-first', 'terminalOrdering:end-first',
            'rejectedAndWrongReplies:rejected', 'rejectedAndWrongReplies:wrong-reply', 'pendingSeekBlocksSettlement',
            'duplicateAndReentrant', 'supersededAndEventLoss:intent', 'supersededAndEventLoss:core',
            'supersededAndEventLoss:event-loss', 'missingIdentityAndRedirect:missing', 'missingIdentityAndRedirect:redirect')
    }
    return @('newerLoadAndStop:newer-load', 'newerLoadAndStop:newer-stop', 'independentPlayerIsolation',
        'staleCoreAndHost:core', 'staleCoreAndHost:host', 'staleCoreAndHost:exiting', 'seekSubmissionFailureCleanup',
        'asyncFailureAndReentrancy', 'newerIntentAfterFailure', 'bindingMustPrecedeArm:before',
        'bindingMustPrecedeArm:after', 'bindingMustPrecedeArm:reentrant', 'queuedStopReplyAfterContainment',
        'pendingLoadSettlesBeforeStop', 'pendingLoadExpiresBeforeStop',
        'containedPendingLoadContinuesOnlyForCurrentOwner:current', 'containedPendingLoadContinuesOnlyForCurrentOwner:deadline',
        'containedPendingLoadContinuesOnlyForCurrentOwner:new-core', 'containedPendingLoadWithoutWakeExpires')
}

function Get-HealthyDiagnosticCases {
    param([string[]]$Targets)
    $groups = @{
        'healthy_graph_capture' = @(
            'uninitializedGraphRejectsCapture', 'positiveHandoff', 'wrongBindingRejected', 'wrongLaunchBindingRejected',
            'framebufferReplacementRevokesBaseline', 'revokedClaimRejectsCapture', 'cleanupRetainsAccessButClosesCapture', 'rendererDestructionRejectsCapture',
            'badBaselineRejected-0', 'badBaselineRejected-1', 'badBaselineRejected-2', 'replayAndContextRejected',
            'changedEntryRejected', 'lateDeliveryAfterContainment', 'slotInvalidatedBeforeRendererIssue', 'slotInvalidatesQueuedTicket',
            'failedClaimInstallationCannotCapture', 'baselinePixelsCannotQualifyTarget', 'targetPixelsBeforeTargetArmRejected', 'sameIdentityRejectsOldRendererGeneration'
        )
        'healthy_unpause_player' = @(
            'actualPausedHoldAndUnpause', 'missingPermitAndImage:missing-permit', 'missingPermitAndImage:missing-image'
        )
        'healthy_graph_capture_lifetime' = @(
            'qualified-destruction', 'incomplete-closure', 'wrong-generation', 'rejected-finalizer',
            'reentrant-release', 'canceled-retirement', 'wrong-thread', 'queued-worker',
            'queued-qt'
        )
        'healthy_player_restore_observation' = @(
            'barrierAndStaleReply', 'failedSeekAndReplacement', 'diagnosticStopReplacement', 'diagnosticLoadReplacement',
            'diagnosticCoreReplacement', 'ordinaryObservation'
        )
        'healthy_player_native_restore' = @(
            'actualObservationBoundary', 'lateAndDuplicateObservation', 'pausedLoadAndExactSeek', 'cancellationBeforeLoad',
            'staleObservationAndGrant', 'cancellationAfterLoad', 'fixtureBridgeControl', 'fixtureBridgeCapture',
            'lateAfterEndAndCoreChange', 'malformedAndUnrelatedReplies', 'reorderedLoadAndNativeConsumption',
            'cancellationDuringSeek', 'reentrantCancellationAndSeekFailure', 'diagnosticPosition1000Rejected', 'diagnosticPosition3125Rejected',
            'diagnosticReentrantObservation'
        )
        'healthy_graph_dispatch' = @(
            'bothGrantsLoadPaused_0', 'bothGrantsLoadPaused_1', 'permitThenLostEndpointRejectsDirectLoad', 'permitThenLostEndpointRejectsReceiptEntry',
            'moduleRevocationBeforeLoadAdmission', 'endpointRevocationBeforeLoadAdmission', 'claimRevocationBeforeLoadAdmission', 'clockCancellationBeforeLoadAdmission',
            'clockRevokesEndpointBeforeLoadAdmission', 'admittedLoadCannotRetry', 'cleanupRetainsOriginalOwnerAfterLoad', 'wrongOwnerRejectsDirectLoad_0',
            'wrongOwnerRejectsDirectLoad_1', 'wrongOwnerRejectsDirectLoad_2', 'wrongOwnerRejectsDirectLoad_3', 'wrongOwnerRejectsDirectLoad_4',
            'replyWithoutStartCannotCapture', 'originalDeadlineRejectsLateLoad', 'pendingLoadExpiresWithoutSuccess', 'bootstrapRejectsMalformed',
            'permitThenLostEndpointRejectsDirectSeek', 'revocationDuringSeekAuthorization_0', 'revocationDuringSeekAuthorization_1', 'sameGenerationDifferentClaimRejectsSeek',
            'clockInvalidatesTargetBeforeSeekAdmission', 'targetAckRequiredBeforeSeek', 'staleTargetAckRejectsSeek', 'cancelAfterTargetArmRejectsSeek',
            'wrongOwnerRejectsDirectSeek_0', 'wrongOwnerRejectsDirectSeek_1', 'wrongOwnerRejectsDirectSeek_2', 'wrongOwnerRejectsDirectSeek_3',
            'wrongOwnerRejectsDirectSeek_4', 'admittedSeekCannotRetry', 'wrongEntryCannotQualifyBaseline', 'preSeekTargetCannotQualify',
            'postSubmitTargetBeforeReplyCanQualify', 'restartBeforeSeekReplyCannotQualify', 'earlyObservationCannotQualify', 'desiredPausedCompletesHold',
            'retainedHoldCannotOutliveTargetQualification', 'otherRendererAckCannotAuthorizeSeek', 'lastLivenessCallbackRevokesLoadAuthority', 'lastLivenessCallbackRevokesClaimBeforeLoad',
            'lastLivenessCallbackExpiresLoadDeadline', 'lastLivenessCallbackInvalidatesTarget', 'moduleCallbackRevokesEndpointBeforeLoad',
            'lastLivenessCallbackClosesSeekAdmission_0', 'lastLivenessCallbackClosesSeekAdmission_1', 'trace-load',
            'trace-seek'
        )
        'healthy_b_channel_dispatch' = @(
            'load-ok', 'load-endpoint', 'load-module', 'load-reentrant',
            'seek-ok', 'seek-endpoint', 'admitted-failed', 'admitted-ok',
            'containment-first', 'failure-first', 'pending-load-expires', 'reentry-before-gate',
            'reentry-after-gate'
        )
        'healthy_graph_arm' = @(
            'cancelBeforeGrant', 'normalPermitBaseline', 'failedCleanupReadinessRejectsPermit', 'playerBindingFailureRetainsUnknown',
            'deadlineAfterArm', 'foreignClaimPreventsArm', 'failureCrossesInstallation', 'installationFailureRetainsCleanupAuthority',
            'reservationPrecedesPlayerArm', 'containmentCrossesInstallation', 'containmentDuringPlayerArm', 'reentrantCancelAfterArm',
            'cancelAfterValidationBeforeArm', 'duplicateCancellationRejected', 'changedCoreCannotSettle', 'wrongGateRejected',
            'internalFailureAfterArm', 'containmentBeforeArm', 'containmentAfterArm'
        )
        'healthy_graph_failure_provenance' = @(
            'ready-loss', 'queued-permit-loss', 'loss-after-arm', 'loss-during-poll',
            'initialization-failure', 'ack-in-flight', 'ack-aborts'
        )
    }
    foreach ($target in $Targets) {
        if ($target -in @('healthy_healthy_restore_cancel', 'healthy_player_restore_cleanup')) {
            foreach ($case in (Get-HealthyCleanupCases $target)) { "$target.$case" }
        } elseif ($groups.ContainsKey($target)) {
            foreach ($case in $groups[$target]) { "$target.$case" }
        } else { $target }
    }
}
