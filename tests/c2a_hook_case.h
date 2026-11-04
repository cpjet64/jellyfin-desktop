#pragma once

static bool writeHookEvidence(const QString& evidence, const QJsonObject& record)
{
  QFile file(evidence + "/c2a-stop-hook.json");
  if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
  const QByteArray bytes = QJsonDocument(record).toJson();
  return file.write(bytes) == bytes.size() && file.flush();
}

static bool recordHookTerminal(mpv_handle* core, int canceledCount, QJsonObject& record)
{
  int idle = 0;
  char* vo = nullptr;
  char* force = nullptr;
  const int idleStatus = mpv_get_property(core, "idle-active", MPV_FORMAT_FLAG, &idle);
  const int voStatus = mpv_get_property(core, "current-vo", MPV_FORMAT_STRING, &vo);
  const int forceStatus = mpv_get_property(core, "force-window", MPV_FORMAT_STRING, &force);
  const QString forceValue = force ? QString::fromUtf8(force) : QString();
  if (vo) mpv_free(vo);
  if (force) mpv_free(force);
  record.insert("idle_status", idleStatus);
  record.insert("idle_active", idle);
  record.insert("current_vo_status", voStatus);
  record.insert("force_window_status", forceStatus);
  record.insert("force_window", forceValue);
  record.insert("canceled_count", canceledCount);
  return idleStatus == 0 && idle == 1 && voStatus == MPV_ERROR_PROPERTY_UNAVAILABLE &&
    forceStatus == 0 && forceValue == "no" && canceledCount == 1;
}

// Included after TestC2RetirementNative so its diagnostic friendship applies.
void TestC2RetirementNative::stopWithPendingHook()
{
  const QString root = qEnvironmentVariable("JELLYFIN_TEST_OWNED_ROOT");
  const QString evidence = qEnvironmentVariable("RECOVERY_EVIDENCE_DIR");
  const QString media = qEnvironmentVariable("RECOVERY_MEDIA");
  const QString rootPath = QFileInfo(root).canonicalFilePath();
  const QString evidencePath = QFileInfo(evidence).canonicalFilePath();
  const QString relative = QDir(rootPath).relativeFilePath(evidencePath);
  QVERIFY(!rootPath.isEmpty() && !evidencePath.isEmpty() && QFileInfo(media).isFile());
  QVERIFY(relative != ".." && !relative.startsWith("../") && !QDir::isAbsolutePath(relative));
  QString error;
  QVERIFY2(MpvTestBootstrap::prepareMpvTestEnvironment(root, &error), qPrintable(error));
  QVERIFY2(prepareMpvTestProfile(evidence, &error), qPrintable(error));
  Q_INIT_RESOURCE(resources);
  QVERIFY(SettingsComponent::Get().componentInitialize());
  PlayerComponent player;
  C2aFixture fixture;
  QVERIFY2(fixture.initialize(player, &error), qPrintable(error));
  auto* controller = fixture.controller;
  auto* core = fixture.core;
  const auto generation = fixture.generation;
  const auto bindingGeneration = player.m_mpvGeneration;
  const bool ac3 = player.m_doAc3Transcoding;
  const auto sameBinding = [&] {
    return fixture.item && fixture.item->controller() == controller && controller->mpv() == core &&
      fixture.item->renderGeneration() == generation && player.m_mpv == controller &&
      player.m_lastMpvController == controller && player.m_mpvGeneration == bindingGeneration &&
      player.m_doAc3Transcoding == ac3 && player.thread() == QThread::currentThread();
  };
  QVERIFY(sameBinding());
  QTRY_VERIFY_WITH_TIMEOUT(!fixture.item->observed->snapshot().isEmpty(), 10000);
  const auto render = fixture.item->observed->snapshot().last();
  QVERIFY(render.context != 0 && render.thread == fixture.scene.audit().renderThread);
  QSignalSpy canceled(&player, &PlayerComponent::canceled);
  player.m_c2HookDelayMs = 3000;
  QVERIFY(fixture.startPlayback(player, QFileInfo(media).absoluteFilePath()));
  QTRY_COMPARE_WITH_TIMEOUT(player.m_c2HooksScheduled, 1, 10000);
  const auto hookId = player.m_c2LastScheduledHookId;
  const auto scheduledNs = QtThreaded::monotonicNanos();
  QVERIFY(player.m_c2HooksCompleted == 0 && player.m_c2HookContinueCalls == 0);
  const auto stop = player.requestNativeStop();
  const auto stopNs = QtThreaded::monotonicNanos();
  QCOMPARE(stop.status, PlayerComponent::NativeStopResult::Status::Accepted);
  QCOMPARE(player.m_c2HooksScheduled, 1);
  QCOMPARE(player.m_c2HooksCompleted, 0);
  QCOMPARE(player.m_c2HookContinueCalls, 0);
  QCOMPARE(player.m_c2LastScheduledHookId, hookId);
  QVERIFY(sameBinding());
  QCOMPARE(mpv_set_property_string(core, "force-window", "no"), 0);
  QTRY_COMPARE_WITH_TIMEOUT(player.m_c2HookContinueCalls, 1, 10000);
  const auto continuedNs = QtThreaded::monotonicNanos();
  QCOMPARE(player.m_c2HooksCompleted, 1);
  QCOMPARE(player.m_c2LastContinuedHookId, hookId);
  QCOMPARE(player.m_c2LastHookContinueResult, 0);
  QVERIFY(fixture.cleanup.confirmTerminal(core, canceled));
  const auto terminalNs = QtThreaded::monotonicNanos();
  QJsonObject terminal;
  QVERIFY(recordHookTerminal(core, canceled.count(), terminal));
  QVERIFY(fixture.cleanup.requestRetirement(generation, sameBinding));
  const auto retiredNs = QtThreaded::monotonicNanos();
  QVERIFY(sameBinding() && !fatalDiagnostic.load());
  const auto beforeCleanup = fixture.scene.audit();
  const QJsonObject observed{{"scenario", "stop-with-pending-hook"},
    {"hook_id", QString::number(hookId)}, {"continued_hook_id", QString::number(player.m_c2LastContinuedHookId)},
    {"scheduled_count", player.m_c2HooksScheduled}, {"completed_count", player.m_c2HooksCompleted},
    {"continue_count", player.m_c2HookContinueCalls}, {"continue_result", player.m_c2LastHookContinueResult},
    {"pending_at_accepted_stop", true}, {"stop_error", stop.error}, {"terminal", terminal},
    {"scheduled_observed_ns", QString::number(scheduledNs)}, {"stop_observed_ns", QString::number(stopNs)},
    {"continued_observed_ns", QString::number(continuedNs)}, {"terminal_observed_ns", QString::number(terminalNs)},
    {"retirement_observed_ns", QString::number(retiredNs)}, {"generation", QString::number(generation)},
    {"binding_generation", QString::number(bindingGeneration)}, {"ac3", ac3},
    {"render_thread", QString::number(beforeCleanup.renderThread, 16)},
    {"gui_thread", QString::number(beforeCleanup.guiThread, 16)},
    {"render_context", QString::number(render.context, 16)},
    {"free_count", DiagnosticFailureInjection::frees.load()}, {"ack_order", DiagnosticFailureInjection::ackOrder.load()}};
  QVERIFY(fixture.cleanup.finish());
  QVERIFY(!fatalDiagnostic.load());
  QVERIFY(writeHookEvidence(evidence, observed));
}
