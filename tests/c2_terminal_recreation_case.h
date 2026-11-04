#pragma once

#include "c2_terminal_recreation.h"

// Diagnostic control: the second Play is explicit test input, not recovery.
void TestC2RetirementNative::terminalRecreation()
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
  C2TerminalRecreation control;
  QVERIFY2(control.initialize(player, &error), qPrintable(error));
  auto* const controller = control.controller;
  auto* const core = control.core;
  const quint64 binding = player.m_mpvGeneration;
  const bool ac3 = player.m_doAc3Transcoding;
  const auto sameBinding = [&] { return player.m_mpv == controller && player.m_mpvGeneration == binding &&
      controller->mpv() == core && control.item->controller() == controller && player.m_doAc3Transcoding == ac3; };
  QVERIFY(sameBinding());
  QCOMPARE(controller->eventMode(), MpvController::EventMode::External);
  QCOMPARE(DiagnosticFailureInjection::frees.load(), 0);
  QSignalSpy canceled(&player, &PlayerComponent::canceled);
  control.playBoundary = QtThreaded::monotonicNanos();
  QVERIFY(control.play(player, media, 0));
  const qint64 firstEntry = control.entry;
  QVERIFY(control.moving(player, evidence + "/c2-terminal-g1", 0));
  const auto renders = control.item->observed->snapshot();
  QVERIFY(!renders.isEmpty());
  const auto original = renders.last();
  QVERIFY(original.context && original.thread && original.returned > original.entered);
  QVERIFY(control.captureFirstCreation(original.thread, original.context));
  QVERIFY(control.terminal(player, canceled, canceled.count()));
  QVERIFY(sameBinding());
  QVERIFY(control.save(evidence + "/c2-terminal-g1-stopped.json", control.terminalEvidence(control.g1)));
  QVERIFY(control.retire(control.g1, 1, original.thread, original.context, sameBinding));
  QVERIFY(control.save(evidence + "/c2-terminal-g1-retired.json", control.retirementEvidence()));
  QVERIFY(control.recreate(original.thread, original.context));
  QVERIFY(control.save(evidence + "/c2-terminal-g2-created.json", control.creationEvidence()));
  QVERIFY(sameBinding());
  const auto creationBoundary = QtThreaded::monotonicNanos();
  QVERIFY(control.sameRenderContextAfter(creationBoundary, original.thread, original.context));
  const quint64 lastSequence = control.scene.frames().isEmpty() ? 0 : control.scene.frames().last().sequence;
  control.playBoundary = QtThreaded::monotonicNanos();
  QVERIFY(control.play(player, media, firstEntry));
  const qint64 secondEntry = control.entry;
  QVERIFY(secondEntry != firstEntry);
  QVERIFY(control.moving(player, evidence + "/c2-terminal-g2", lastSequence));
  QVERIFY(control.terminal(player, canceled, canceled.count()));
  QVERIFY(sameBinding());
  QVERIFY(control.save(evidence + "/c2-terminal-g2-stopped.json", control.terminalEvidence(control.g2)));
  QVERIFY(control.retire(control.g2, 2, original.thread, original.context, sameBinding));
  QVERIFY(control.save(evidence + "/c2-terminal-g2-retired.json", control.retirementEvidence()));
  QCOMPARE(control.created->count(), 1);
  QCOMPARE(control.retired->count(), 2);
  QVERIFY(control.failures->isEmpty());
  QVERIFY(control.finish());
  QVERIFY(DiagnosticFailureInjection::ackOrder.load() < DiagnosticFailureInjection::itemOrder.load());
  QVERIFY(DiagnosticFailureInjection::itemOrder.load() < DiagnosticFailureInjection::coreOrder.load());
  QVERIFY(player.m_mpv == nullptr && player.m_mpvGeneration > binding);
  QVERIFY(!fatalDiagnostic.load());
}
