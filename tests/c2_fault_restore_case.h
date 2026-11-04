#pragma once

#include "c2_fault_restore_runtime.h"
#include "c2_replay_recipe.h"

namespace {
bool saveRestoreCapture(C2FaultRestoreRuntime& control, const C2ReplayRecipe& recipe, const QString& mediaPath,
                        quint64 binding, const MpvRenderEvent& render, const QString& evidence)
{
  return control.save(evidence + "/c2-restore-g1-capture.json", {{"entry", control.entry},
    {"position_seconds", recipe.position}, {"pause", recipe.wasPaused}, {"playing_intent", true},
    {"path", mediaPath}, {"binding", qint64(binding)}, {"generation", qint64(control.g1)},
    {"core", QString::number(reinterpret_cast<quintptr>(control.core), 16)},
    {"controller", QString::number(reinterpret_cast<quintptr>(control.controller), 16)},
    {"thread", QString::number(render.thread, 16)}, {"context", QString::number(render.context, 16)},
    {"first_sequence", qint64(control.firstFrame.sequence)}, {"second_sequence", qint64(control.secondFrame.sequence)},
    {"first_ns", control.firstFrame.monotonicNs}, {"second_ns", control.secondFrame.monotonicNs},
    {"start_ms", control.startPosition}, {"end_ms", control.endPosition}});
}

bool finishSyntheticRestore(C2FaultRestoreRuntime& control, PlayerComponent& player, QSignalSpy& canceled,
                            C2ReplayRecipe& recipe, C2FaultRestorePermission& permission,
                            const std::function<C2FaultRestorePermission::Identity()>& currentIdentity,
                            const std::function<bool()>& sameBinding, const QString& evidence)
{
  const auto now = [] { return QtThreaded::monotonicNanos(); };
  const auto valid = [&] { return !fatalDiagnostic.load() && sameBinding() && control.item->renderGeneration() == control.g2 &&
    control.failures->count() == 1 && canceled.count() == 1 && recipe.sameFile(control.core) &&
    control.oneLoadedEntry(control.entry) && control.paused() && permission.mayContinue(currentIdentity(), control.g2, now()); };
  if (!valid()) return false;
  const quint64 sequence = control.scene.frames().isEmpty() ? 0 : control.scene.frames().last().sequence;
  const qint64 boundary = now();
  if (!control.exactSeek(recipe.position) || !control.restoredFrame(recipe.position, sequence, boundary,
      evidence + "/c2-restore-paused.png") || !valid()) return false;
  if (!control.save(evidence + "/c2-restore-position.json", {{"snapshot_seconds", recipe.position},
      {"actual_seconds", control.restoredPosition}, {"frame_index", control.restoredIndex},
      {"frame_seconds", double(control.restoredIndex) / 30.0}, {"tolerance_seconds", 0.100},
      {"boundary_ns", control.restoredBoundary}, {"frame_ns", control.restoredNs},
      {"frame_sequence", qint64(control.restoredSequence)}, {"prior_sequence", qint64(sequence)}})) return false;
  if (!valid()) return false;
  const auto frames = control.scene.frames();
  const quint64 after = frames.isEmpty() ? 0 : frames.last().sequence;
  control.playBoundary = now();
  int pause = 0;
  if (mpv_set_property(control.core, "pause", MPV_FORMAT_FLAG, &pause) < 0 ||
      !control.moving(player, evidence + "/c2-restore-resume", after) ||
      control.failures->count() != 1 || !sameBinding() ||
      !control.terminal(player, canceled, 1)) return false;
  if (!control.save(evidence + "/c2-restore-resumed.json", {{"start_ms", control.startPosition},
      {"end_ms", control.endPosition}, {"first_sequence", qint64(control.firstFrame.sequence)},
      {"second_sequence", qint64(control.secondFrame.sequence)}, {"first_ns", control.firstFrame.monotonicNs},
      {"second_ns", control.secondFrame.monotonicNs}, {"resume_boundary_ns", control.playBoundary}})) return false;
  if (!control.save(evidence + "/c2-restore-g2-stopped.json", control.terminalEvidence(control.g2)) ||
      !control.retire(control.g2, 2, control.firstCreateThread, control.firstCreateContext, sameBinding) ||
      !control.save(evidence + "/c2-restore-g2-retired.json", control.retirementEvidence())) return false;
  if (control.created->count() != 1 || control.retired->count() != 2 || control.failures->count() != 1 ||
      DiagnosticFailureInjection::frees.load() != 2 || !control.finish() || control.observer->client ||
      fatalDiagnostic.load()) return false;
  const auto audit = control.scene.audit();
  const int ack = DiagnosticFailureInjection::ackOrder.load();
  const int item = DiagnosticFailureInjection::itemOrder.load();
  const int core = DiagnosticFailureInjection::coreOrder.load();
  if (!(ack > 0 && ack < item && item < core && audit.cleanupAckOrder > 0 &&
        audit.cleanupAckOrder < audit.workerJoinedOrder && audit.workerJoinedOrder < audit.surfaceDestroyedOrder)) return false;
  return control.save(evidence + "/c2-restore-cleanup.json", {{"observer_closed", true},
    {"free_count", DiagnosticFailureInjection::frees.load()}, {"retired_count", control.retired->count()},
    {"ack_order", ack}, {"item_order", item}, {"core_order", core},
    {"cleanup_ack_order", qint64(audit.cleanupAckOrder)}, {"worker_join_order", qint64(audit.workerJoinedOrder)},
    {"surface_destroy_order", qint64(audit.surfaceDestroyedOrder)}});
}
}

void TestC2RetirementNative::syntheticRestore()
{
  const QString root = qEnvironmentVariable("JELLYFIN_TEST_OWNED_ROOT");
  const QString evidence = qEnvironmentVariable("RECOVERY_EVIDENCE_DIR");
  const QString media = qEnvironmentVariable("RECOVERY_MEDIA");
  const QString rootPath = QFileInfo(root).canonicalFilePath();
  const QString evidencePath = QFileInfo(evidence).canonicalFilePath();
  const QString mediaPath = QFileInfo(media).canonicalFilePath();
  QVERIFY(!rootPath.isEmpty() && !evidencePath.isEmpty() && !mediaPath.isEmpty() && QFileInfo(media).isFile());
  for (const auto& path : {evidencePath, mediaPath}) {
    const QString relative = QDir(rootPath).relativeFilePath(path);
    QVERIFY(relative != ".." && !relative.startsWith("../") && !QDir::isAbsolutePath(relative));
  }
  QString error;
  QVERIFY2(MpvTestBootstrap::prepareMpvTestEnvironment(root, &error), qPrintable(error));
  QVERIFY2(prepareMpvTestProfile(evidence, &error), qPrintable(error));
  Q_INIT_RESOURCE(resources);
  QVERIFY(SettingsComponent::Get().componentInitialize());
  PlayerComponent player;
  C2FaultRestoreRuntime control;
  QVERIFY2(control.initialize(player, &error), qPrintable(error));
  const auto binding = player.m_mpvGeneration;
  const bool ac3 = player.m_doAc3Transcoding;
  const auto sameBinding = [&] { return player.m_mpv == control.controller &&
    player.m_mpvGeneration == binding && control.controller->mpv() == control.core &&
    control.item->controller() == control.controller && player.m_doAc3Transcoding == ac3 &&
    control.controller->eventMode() == MpvController::EventMode::External; };
  QVERIFY(sameBinding());
  QSignalSpy canceled(&player, &PlayerComponent::canceled);
  control.playBoundary = QtThreaded::monotonicNanos();
  QVERIFY(control.play(player, mediaPath, 0));
  QVERIFY(control.moving(player, evidence + "/c2-restore-g1", 0));
  QVERIFY(control.wait([&] { return player.getPosition() >= 1900; }));
  C2ReplayRecipe recipe;
  C2CaptureObservation capture;
  const bool captured = recipe.capture(control.core, mediaPath, C2ReplayRecipe::Intent::Playing, control.entry, &capture);
  QVERIFY(control.save(evidence + "/c2-restore-capture-observation.json", capture.record()));
  QVERIFY(captured);
  QVERIFY(!recipe.wasPaused && recipe.position >= 1.9 && recipe.position <= 2.5);
  const auto renders = control.item->observed->snapshot();
  QVERIFY(!renders.isEmpty());
  const auto render = renders.last();
  QVERIFY(render.thread && render.context && render.returned > render.entered);
  QVERIFY(control.captureFirstCreation(render.thread, render.context));
  QVERIFY(saveRestoreCapture(control, recipe, mediaPath, binding, render, evidence));
  C2FaultRestorePermission::Identity identity{reinterpret_cast<std::uintptr_t>(control.core),
    reinterpret_cast<std::uintptr_t>(control.controller), binding, control.g1, control.entry,
    mediaPath.toStdString(), recipe.position, true};
  const auto currentIdentity = [&] {
    auto current = identity;
    current.core = reinterpret_cast<std::uintptr_t>(control.controller->mpv());
    current.controller = reinterpret_cast<std::uintptr_t>(player.m_mpv);
    current.binding = player.m_mpvGeneration;
    current.g1 = control.g1;
    return current;
  };
  C2FaultRestorePermission permission(identity, QtThreaded::monotonicNanos() + 60000000000LL);
  QVERIFY(control.quiesce(player, canceled, render.thread, render.context, sameBinding, evidence));
  QVERIFY(!fatalDiagnostic.load());
  QVERIFY(control.recreateAfterSynthetic(render.thread, render.context));
  QVERIFY(control.save(evidence + "/c2-restore-g2-created.json", control.creationEvidence()));
  const bool terminal = control.stoppedState(canceled, sameBinding);
  const bool selected = control.failures->count() == 1 &&
    control.failures->at(0).at(0).toULongLong() == control.g1 &&
    control.failures->at(0).at(1).value<MpvAbstractItem::RenderFailureStage>() == MpvAbstractItem::RenderFailureStage::Render &&
    control.failures->at(0).at(2).toInt() == MPV_ERROR_UNSUPPORTED;
  C2FaultRestorePermission::Proof proof{currentIdentity(), control.g2, selected, canceled.count() == 1,
    terminal, DiagnosticFailureInjection::serviceCloseNs.load() > 0,
    control.retired->count() == 1 && control.retired->at(0).at(0).toULongLong() == control.g1,
    control.created->count() == 1 && control.creationAckOnGui};
  QVERIFY(!fatalDiagnostic.load() && sameBinding() && terminal && permission.consume(proof, QtThreaded::monotonicNanos()));
  const bool loaded = control.reloadPaused(mediaPath.toUtf8(), identity.entry);
  permission.reloadResult(loaded);
  QVERIFY(loaded);
  QVERIFY(finishSyntheticRestore(control, player, canceled, recipe, permission, currentIdentity, sameBinding, evidence));
  QVERIFY(player.m_mpv == nullptr && player.m_mpvGeneration > binding);
}
