"""Run the isolated, injected healthy-restart diagnostic gate on Unix."""

import os
import re
from pathlib import Path
import shutil
import xml.etree.ElementTree as ET

from check_results import check_junit
from explicit_mpv import cmake_options, default_options, explicit_inputs, verify_cache
from headless_requirements import missing_build_inputs
from native_inventory import incompatible_tools, unavailable_compilers


# These are QCoreApplication/injected tests; no mpv-controller or held fixture.
DIAGNOSTIC_TARGETS = (
    "healthy_async_stop_player", "healthy_unpause_player", "healthy_fixture_adapter_cleanup",
    "healthy_healthy_restore_cancel", "healthy_async_stop_source", "healthy_player_native_receipts",
    "healthy_player_restore_cleanup", "healthy_player_restore_observation", "healthy_player_native_restore",
    "healthy_graph_capture", "healthy_graph_capture_lifetime", "healthy_graph_dispatch",
)

LIFETIME_CASES = (
    'qualified-destruction', 'incomplete-closure', 'wrong-generation', 'rejected-finalizer',
    'reentrant-release', 'canceled-retirement', 'wrong-thread', 'queued-worker', 'queued-qt',
)
CAPTURE_CASES = (
    'uninitializedGraphRejectsCapture', 'positiveHandoff', 'wrongBindingRejected', 'wrongLaunchBindingRejected',
    'framebufferReplacementRevokesBaseline', 'revokedClaimRejectsCapture', 'cleanupRetainsAccessButClosesCapture',
    'rendererDestructionRejectsCapture', *(f'badBaselineRejected-{i}' for i in range(3)),
    'replayAndContextRejected', 'changedEntryRejected', 'lateDeliveryAfterContainment',
    'slotInvalidatedBeforeRendererIssue', 'slotInvalidatesQueuedTicket', 'failedClaimInstallationCannotCapture',
    'baselinePixelsCannotQualifyTarget', 'targetPixelsBeforeTargetArmRejected',
    'sameIdentityRejectsOldRendererGeneration',
)
UNPAUSE_CASES = ('actualPausedHoldAndUnpause', 'missingPermitAndImage:missing-permit',
                 'missingPermitAndImage:missing-image')
CANCEL_CASES = (
    'beforeLoad', 'canceledGateCannotBind', *(f'noLoadRejectsUnsolicitedEvents:{row}' for row in ('start', 'end', 'reply')),
    'reentrantQueriesRequireLaterDrain', 'submittedWithoutStart', 'bufferedStartBeforeReply',
    *(f'lateEntryDuringTerminalQuery:{row}' for row in ('end', 'start')),
    *(f'terminalOrdering:{row}' for row in ('reply-first', 'end-first')),
    *(f'rejectedAndWrongReplies:{row}' for row in ('rejected', 'wrong-reply')),
    'pendingSeekBlocksSettlement', 'duplicateAndReentrant',
    *(f'supersededAndEventLoss:{row}' for row in ('intent', 'core', 'event-loss')),
    *(f'missingIdentityAndRedirect:{row}' for row in ('missing', 'redirect')),
)
CLEANUP_CASES = (
    'newerLoadAndStop:newer-load', 'newerLoadAndStop:newer-stop', 'independentPlayerIsolation',
    *(f'staleCoreAndHost:{row}' for row in ('core', 'host', 'exiting')), 'seekSubmissionFailureCleanup',
    'asyncFailureAndReentrancy', 'newerIntentAfterFailure',
    *(f'bindingMustPrecedeArm:{row}' for row in ('before', 'after', 'reentrant')),
    'queuedStopReplyAfterContainment', 'pendingLoadSettlesBeforeStop', 'pendingLoadExpiresBeforeStop',
    *(f'containedPendingLoadContinuesOnlyForCurrentOwner:{row}' for row in ('current', 'deadline', 'new-core')),
    'containedPendingLoadWithoutWakeExpires',
)
OBSERVATION_CASES = (
    'barrierAndStaleReply', 'failedSeekAndReplacement', 'diagnosticStopReplacement',
    'diagnosticLoadReplacement', 'diagnosticCoreReplacement', 'ordinaryObservation',
)
RESTORE_CASES = (
    'actualObservationBoundary', 'lateAndDuplicateObservation', 'pausedLoadAndExactSeek',
    'cancellationBeforeLoad', 'staleObservationAndGrant', 'cancellationAfterLoad',
    'fixtureBridgeControl', 'fixtureBridgeCapture',
    'lateAfterEndAndCoreChange', 'malformedAndUnrelatedReplies', 'reorderedLoadAndNativeConsumption',
    'cancellationDuringSeek', 'reentrantCancellationAndSeekFailure', 'diagnosticPosition1000Rejected',
    'diagnosticPosition3125Rejected', 'diagnosticReentrantObservation',
)
DISPATCH_CASES = (
    'bothGrantsLoadPaused_0', 'bothGrantsLoadPaused_1', 'permitThenLostEndpointRejectsDirectLoad',
    'permitThenLostEndpointRejectsReceiptEntry', 'moduleRevocationBeforeLoadAdmission',
    'endpointRevocationBeforeLoadAdmission', 'claimRevocationBeforeLoadAdmission',
    'clockCancellationBeforeLoadAdmission', 'clockRevokesEndpointBeforeLoadAdmission',
    'admittedLoadCannotRetry', 'cleanupRetainsOriginalOwnerAfterLoad',
    *(f'wrongOwnerRejectsDirectLoad_{i}' for i in range(5)), 'replyWithoutStartCannotCapture',
    'originalDeadlineRejectsLateLoad', 'pendingLoadExpiresWithoutSuccess', 'bootstrapRejectsMalformed',
    'permitThenLostEndpointRejectsDirectSeek', *(f'revocationDuringSeekAuthorization_{i}' for i in range(2)),
    'sameGenerationDifferentClaimRejectsSeek', 'clockInvalidatesTargetBeforeSeekAdmission',
    'targetAckRequiredBeforeSeek', 'staleTargetAckRejectsSeek', 'cancelAfterTargetArmRejectsSeek',
    *(f'wrongOwnerRejectsDirectSeek_{i}' for i in range(5)), 'admittedSeekCannotRetry',
    'wrongEntryCannotQualifyBaseline', 'preSeekTargetCannotQualify', 'postSubmitTargetBeforeReplyCanQualify',
    'restartBeforeSeekReplyCannotQualify', 'earlyObservationCannotQualify', 'desiredPausedCompletesHold',
    'retainedHoldCannotOutliveTargetQualification', 'otherRendererAckCannotAuthorizeSeek',
    'lastLivenessCallbackRevokesLoadAuthority', 'lastLivenessCallbackRevokesClaimBeforeLoad',
    'lastLivenessCallbackExpiresLoadDeadline', 'lastLivenessCallbackInvalidatesTarget',
    'moduleCallbackRevokesEndpointBeforeLoad', 'lastLivenessCallbackClosesSeekAdmission_0',
    'lastLivenessCallbackClosesSeekAdmission_1', 'trace-load', 'trace-seek',
)
CASE_GROUPS = {
    'healthy_graph_capture': CAPTURE_CASES,
    'healthy_unpause_player': UNPAUSE_CASES,
    'healthy_healthy_restore_cancel': CANCEL_CASES,
    'healthy_player_restore_cleanup': CLEANUP_CASES,
    'healthy_graph_capture_lifetime': LIFETIME_CASES,
    'healthy_player_restore_observation': OBSERVATION_CASES,
    'healthy_player_native_restore': RESTORE_CASES,
    'healthy_graph_dispatch': DISPATCH_CASES,
}
DIAGNOSTIC_TESTS = tuple(name for target in DIAGNOSTIC_TARGETS
                         for name in (tuple(f'{target}.{case}' for case in CASE_GROUPS[target])
                                      if target in CASE_GROUPS else (target,)))


def require_prerequisites(system, root, environment):
    found = {name: shutil.which(name) for name in ("cmake", "ctest", "ninja", "c++", "git", "pkg-config")}
    missing = [name for name in ("cmake", "ctest", "ninja", "c++", "git") if not found[name]]
    missing += incompatible_tools(found)
    missing += unavailable_compilers(environment)
    explicit = explicit_inputs(environment, system, root)
    missing += missing_build_inputs("static", environment, found["pkg-config"], bool(explicit))
    if missing:
        raise RuntimeError("BLOCKED: diagnostic prerequisites unavailable: " + ", ".join(missing))
    return explicit


def verify_report(path, expected):
    try:
        check_junit(path, expected)
        suite = ET.parse(path).getroot()
        cases = suite.findall("testcase")
        names = [case.get("name") for case in cases]
    except (OSError, ET.ParseError, ValueError) as error:
        raise RuntimeError("Diagnostic CTest report is missing or unsuccessful") from error
    if (suite.tag != "testsuite" or suite.findall(".//testsuite") or
            len(suite.findall(".//testcase")) != len(cases) or suite.get("tests") != str(len(expected)) or
            suite.get("failures") != "0" or
            any(suite.get(field, "0") != "0" for field in ("errors", "disabled", "skipped")) or
            any(case.get("status") != "run" for case in cases) or
            len(names) != len(expected) or set(names) != set(expected)):
        raise RuntimeError("Diagnostic CTest report does not exactly match required tests")


def run(system, root, build, results, command, environment):
    if system not in ("Linux", "Darwin"):
        raise RuntimeError("Unsupported diagnostic platform")
    if not DIAGNOSTIC_TESTS or len(set(DIAGNOSTIC_TESTS)) != len(DIAGNOSTIC_TESTS):
        raise RuntimeError("Diagnostic test selection is empty or duplicated")
    explicit = require_prerequisites(system, root, environment)
    diagnostic = build / "healthy-diagnostic"
    report = results / "healthy-diagnostic-ctest.xml"
    if diagnostic.exists() or diagnostic.is_symlink() or report.exists() or report.is_symlink():
        raise RuntimeError("BLOCKED: diagnostic output already exists")
    options = ["cmake", "-S", str(root), "-B", str(diagnostic), "-G", "Ninja",
               "-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DHEALTHY_RESTART_DIAGNOSTIC_ONLY=ON",
               "-DPLAYER_INTENT_DIAGNOSTIC_ONLY=ON", "-DUSE_STATIC_MPVQT=ON", "-DMPVQT_EXTERNAL_EVENTS=ON"]
    options.extend(cmake_options(explicit) if explicit else default_options(diagnostic / "CMakeCache.txt"))
    prefixes = [environment[key] for key in ("CI_QT_PREFIX", "CI_MPV_PREFIX") if environment.get(key)]
    prefixes += [item for item in environment.get("CMAKE_PREFIX_PATH", "").split(os.pathsep) if item]
    if prefixes:
        options.append("-DCMAKE_PREFIX_PATH=" + ";".join(prefixes))
    command("healthy-diagnostic-configure", options, timeout=300)
    if explicit:
        verify_cache(diagnostic / "CMakeCache.txt", explicit, "static")
    command("healthy-diagnostic-build", ["cmake", "--build", str(diagnostic), "--parallel", "4", "--target",
                                         *DIAGNOSTIC_TARGETS], timeout=1800)
    owned = diagnostic / "test-owned"
    owned.mkdir()
    test_env = environment.copy()
    test_env["JELLYFIN_TEST_OWNED_ROOT"] = str(owned)
    expression = "^(" + "|".join(map(re.escape, DIAGNOSTIC_TESTS)) + ")$"
    command("healthy-diagnostic-ctest", ["ctest", "--test-dir", str(diagnostic), "-R", expression,
            "--no-tests=error", "--timeout", "60", "--output-on-failure", "--output-junit", str(report)],
            env=test_env, timeout=900)
    verify_report(report, DIAGNOSTIC_TESTS)
