void TestC2RetirementNative::verifyFailedCreate(Scene& scene, quint64 first, QSignalSpy& failed,
                                                QSignalSpy& noContext, QSignalSpy& created)
{
  QCOMPARE(failed.count(), 1);
  QCOMPARE(failed.at(0).at(1).value<MpvAbstractItem::RenderFailureStage>(), MpvAbstractItem::RenderFailureStage::Create);
  QCOMPARE(failed.at(0).at(2).toInt(), int(MPV_ERROR_UNSUPPORTED));
  QVERIFY(scene.waitFor([&] { return !noContext.isEmpty(); }, 10000));
  QCOMPARE(noContext.count(), 1);
  QCOMPARE(noContext.at(0).at(0).toULongLong(), first);
  QCOMPARE(noContext.at(0).at(2).toInt(), int(MPV_ERROR_UNSUPPORTED));
  QVERIFY(created.isEmpty());
  QVERIFY(scene.item->requestRenderCreation(first) != MpvAbstractItem::CreationRequest::Accepted);
  for (int i = 0; i < 3; ++i) QVERIFY(scene.frame());
  QCOMPARE(failed.count(), 1);
  QVERIFY(scene.destroyItem());
}
void TestC2RetirementNative::verifyStopped(Scene& scene, C2ReplayRecipe& recipe,
                                           C2EventObserver& observer, mpv_handle* core, bool previousAttempt)
{
  QCOMPARE(recipe.afterReady(core, observer), C2ReplayRecipe::Outcome::Cancelled);
  QCOMPARE(recipe.attempted, previousAttempt);
  observer.drain();
  const int starts = observer.count(MPV_EVENT_START_FILE);
  QVERIFY(scene.waitFor([&] {
    const auto idle = observer.flag("idle-active");
    return idle.error == 0 && idle.value;
  }, 10000));
  QElapsedTimer stable;
  stable.start();
  while (stable.elapsed() < 1000) {
    QVERIFY(scene.frame());
    QTest::qWait(20);
    const auto idle = observer.flag("idle-active");
    QCOMPARE(idle.error, 0);
    QVERIFY(idle.value);
    QCOMPARE(recipe.afterReady(core, observer), C2ReplayRecipe::Outcome::Cancelled);
    observer.drain();
    QCOMPARE(observer.count(MPV_EVENT_START_FILE), starts);
  }
}

void TestC2RetirementNative::holdStaleCallback()
{
  DiagnosticFailureInjection::holdDeferredUpdate = true;
  DiagnosticFailureInjection::deferNextQueuedUpdate = true;
  const int queued = DiagnosticFailureInjection::updatesQueued.load();
  QElapsedTimer deadline;
  deadline.start();
  while (DiagnosticFailureInjection::updatesQueued.load() == queued && deadline.elapsed() < 5000)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  QVERIFY(DiagnosticFailureInjection::updatesQueued.load() > queued);
}

void TestC2RetirementNative::verifyStaleCallback(Scene& scene, PlayerComponent& player,
                                                  MpvController* controller, quint64 binding,
                                                  int destroyedBefore, int rejectedBefore,
                                                  QSignalSpy& retired)
{
  DiagnosticFailureInjection::holdDeferredUpdate = false;
  QVERIFY(scene.waitFor([&] { return DiagnosticFailureInjection::deferredUpdatesRejected.load() > rejectedBefore; }, 10000));
  QVERIFY(scene.waitFor([&] { return DiagnosticFailureInjection::ownersDestroyed.load() > destroyedBefore; }, 10000));
  QCOMPARE(DiagnosticFailureInjection::frees.load(), 1);
  QCOMPARE(retired.count(), 1);
  QCOMPARE(player.m_mpv, controller);
  QCOMPARE(player.m_mpvGeneration, binding);
}

static int brightPixels(const QImage& image)
{
  int bright = 0;
  for (int y = 0; y < image.height(); y += 4)
    for (int x = 0; x < image.width(); x += 4) {
      const QColor pixel = image.pixelColor(x, y);
      if (pixel.red() + pixel.green() + pixel.blue() > 300) ++bright;
    }
  return bright;
}

void TestC2RetirementNative::verifyRecovery(Scene& scene, PlayerComponent& player,
                                              C2ReplayRecipe& recipe, const QString& scenario,
                                              const QString& evidence)
{
  QImage first;
  QVERIFY(scene.waitFor([&] {
    first = scene.target->toImage();
    return brightPixels(first) > 100 && qAbs(player.getPosition() / 1000.0 - recipe.position) < 1.0;
  }, 10000));
  QVERIFY(first.save(evidence + "/c2b-restored.png"));
  const auto initial = player.getPosition();
  if (scenario == "paused") {
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < 5200) {
      QVERIFY(scene.frame());
      QTest::qWait(20);
    }
    QVERIFY(qAbs(player.getPosition() - initial) < 100);
    QVERIFY(brightPixels(scene.target->toImage()) > 100);
    return;
  }
  QVERIFY(recipe.resume(scene.item->controller()->mpv()));
  QVERIFY(scene.waitFor([&] { return player.getPosition() > initial + 500; }, 10000));
  const QImage after = scene.target->toImage();
  QVERIFY(brightPixels(after) > 100);
  QVERIFY(after.save(evidence + "/c2b-after.png"));
  const auto later = player.getPosition();
  QVERIFY(scene.waitFor([&] { return player.getPosition() > later + 200; }, 5000));
  const QImage after2 = scene.target->toImage();
  QVERIFY(brightPixels(after2) > 100);
  QVERIFY(after != after2);
  QVERIFY(after2.save(evidence + "/c2b-after2.png"));
}

void TestC2RetirementNative::recovery()
{
  const QString scenario = qEnvironmentVariable("C2_RECOVERY_SCENARIO");
  QVERIFY(scenario == "playing" || scenario == "replay" || scenario == "paused" || scenario == "stop" || scenario == "stop-restore" ||
          scenario == "create-failure" || scenario == "render-failure" || scenario == "stale");
  const QString evidence = qEnvironmentVariable("RECOVERY_EVIDENCE_DIR");
  const QString media = qEnvironmentVariable("RECOVERY_MEDIA");
  const QString root = qEnvironmentVariable("JELLYFIN_TEST_OWNED_ROOT", evidence);
  QVERIFY(QFileInfo(evidence).isDir());
  QVERIFY(QFileInfo(media).isFile());
  QString error;
  QVERIFY2(MpvTestBootstrap::prepareMpvTestEnvironment(root, &error), qPrintable(error));
  QVERIFY2(prepareMpvTestProfile(evidence, &error), qPrintable(error));
  Q_INIT_RESOURCE(resources);
  QVERIFY(SettingsComponent::Get().componentInitialize());
  PlayerComponent player;
  QVERIFY(player.componentInitialize());
  Scene scene;
  QVERIFY(scene.init());
  player.setWindow(scene.window.get());
  struct DeferredRelease {
    ~DeferredRelease() { DiagnosticFailureInjection::holdDeferredUpdate = false; }
  } release;
  recoverScenario(scene, player, scenario, media, evidence);
}

void TestC2RetirementNative::recoverScenario(Scene& scene, PlayerComponent& player,
                                               const QString& scenario, const QString& media,
                                               const QString& evidence)
{
  QSignalSpy ready(scene.item, &MpvAbstractItem::ready);
  QSignalSpy retired(scene.item, &MpvAbstractItem::renderRetired);
  QSignalSpy created(scene.item, &MpvAbstractItem::renderCreated);
  QSignalSpy noContext(scene.item, &MpvAbstractItem::renderCreationFailedNoContext);
  auto* controller = scene.item->controller();
  mpv_handle* core = controller->mpv();
  const auto binding = player.m_mpvGeneration;
  const auto first = scene.item->renderGeneration();
  scene.mayHaveContext = true;
  QVERIFY(scene.waitFor([&] { return !ready.isEmpty(); }, 15000));
  C2EventObserver observer(core);
  QVERIFY(observer.client);
  QVERIFY(player.load(QUrl::fromLocalFile(media).toString(), {{"autoplay", true}}, {{"type", "video"}}));
  QVERIFY(scene.waitFor([&] { return player.getPosition() > 500; }, 10000));
  if (scenario == "paused") {
    int paused = 1;
    QCOMPARE(mpv_set_property(core, "pause", MPV_FORMAT_FLAG, &paused), 0);
  }
  observer.drain();
  QVERIFY(observer.count(MPV_EVENT_START_FILE) > 0);
  QVERIFY(observer.count(MPV_EVENT_FILE_LOADED) > 0);
  observer.mark("before-retirement");
  int64_t entryId = 0;
  for (const auto& event : observer.events)
    if (event.kind == MPV_EVENT_START_FILE) entryId = event.playlistId;
  C2ReplayRecipe recipe;
  QVERIFY(recipe.capture(core, media, scenario == "paused" ? C2ReplayRecipe::Intent::Paused :
                                                        C2ReplayRecipe::Intent::Playing, entryId));
  const int destroyedBefore = DiagnosticFailureInjection::ownersDestroyed.load();
  const int rejectedBefore = DiagnosticFailureInjection::deferredUpdatesRejected.load();
  if (scenario == "stale") holdStaleCallback();
  scene.item->requestRenderRetirement(first);
  QVERIFY(scene.waitFor([&] { return !retired.isEmpty(); }, 10000));
  scene.retired = true;
  observer.mark("retirement-acknowledged");
  if (scenario == "replay" || scenario == "stop-restore") {
    double speed = 4.0;
    QCOMPARE(mpv_set_property(core, "speed", MPV_FORMAT_DOUBLE, &speed), 0);
    QVERIFY(scene.waitFor([&] { const auto idle = observer.flag("idle-active"); return idle.error == 0 && idle.value; }, 15000));
    observer.drain();
    speed = 1.0;
    QCOMPARE(mpv_set_property(core, "speed", MPV_FORMAT_DOUBLE, &speed), 0);
  }
  if (scenario == "stop") {
    recipe.stop();
    QCOMPARE(player.requestNativeStop().status, PlayerComponent::NativeStopResult::Status::Accepted);
    observer.mark("stop-accepted");
  }
  if (scenario == "create-failure") DiagnosticFailureInjection::create = MPV_ERROR_UNSUPPORTED;
  if (scenario == "render-failure") DiagnosticFailureInjection::render = MPV_ERROR_UNSUPPORTED;
  QSignalSpy failed(scene.item, &MpvAbstractItem::renderFailure);
  QCOMPARE(scene.item->requestRenderCreation(first), MpvAbstractItem::CreationRequest::Accepted);
  QVERIFY(scene.waitFor([&] { return !created.isEmpty() || !failed.isEmpty(); }, 15000));
  if (scenario == "create-failure") {
    verifyFailedCreate(scene, first, failed, noContext, created);
    return;
  }
  QVERIFY(!created.isEmpty());
  QVERIFY(scene.item->requestRenderCreation(first) != MpvAbstractItem::CreationRequest::Accepted);
  scene.retired = false;
  QCOMPARE(scene.item->controller(), controller);
  QCOMPARE(controller->mpv(), core);
  QCOMPARE(player.m_mpv, controller);
  QCOMPARE(player.m_mpvGeneration, binding);
  if (scenario == "stale") verifyStaleCallback(scene, player, controller, binding, destroyedBefore, rejectedBefore, retired);
  finishRecovery(scene, player, recipe, observer, scenario, evidence, failed, retired);
}

void TestC2RetirementNative::finishRecovery(Scene& scene, PlayerComponent& player,
                                            C2ReplayRecipe& recipe, C2EventObserver& observer,
                                            const QString& scenario, const QString& evidence,
                                            QSignalSpy& failed, QSignalSpy& retired)
{
  mpv_handle* core = scene.item->controller()->mpv();
  const auto outcome = scenario == "render-failure" ? C2ReplayRecipe::Outcome::Failed :
                                                      recipe.afterReady(core, observer);
  if (scenario == "render-failure") {
    QVERIFY(scene.waitFor([&] { return !failed.isEmpty(); }, 10000));
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.at(0).at(1).value<MpvAbstractItem::RenderFailureStage>(), MpvAbstractItem::RenderFailureStage::Render);
    QCOMPARE(failed.at(0).at(2).toInt(), int(MPV_ERROR_UNSUPPORTED));
    for (int i = 0; i < 3; ++i) QVERIFY(scene.frame());
    QCOMPARE(failed.count(), 1);
    QVERIFY(!recipe.attempted);
  } else if (scenario == "stop") {
    QCOMPARE(outcome, C2ReplayRecipe::Outcome::Cancelled);
    verifyStopped(scene, recipe, observer, core, false);
  } else {
    QVERIFY(outcome == C2ReplayRecipe::Outcome::Continued || outcome == C2ReplayRecipe::Outcome::LoadAccepted);
    if (scenario == "replay" || scenario == "stop-restore") QCOMPARE(outcome, C2ReplayRecipe::Outcome::LoadAccepted);
    if (outcome == C2ReplayRecipe::Outcome::LoadAccepted)
      QVERIFY(scene.waitFor([&] { observer.drain(); return observer.count(MPV_EVENT_FILE_LOADED) > 1; }, 10000));
    if (scenario == "stop-restore") {
      recipe.stop();
      QCOMPARE(player.requestNativeStop().status, PlayerComponent::NativeStopResult::Status::Accepted);
      observer.mark("stop-during-restoration");
      verifyStopped(scene, recipe, observer, core, true);
    } else {
      QVERIFY(recipe.restore(core));
      verifyRecovery(scene, player, recipe, scenario, evidence);
    }
  }
  scene.item->requestRenderRetirement(scene.item->renderGeneration());
  QVERIFY(scene.waitFor([&] { return retired.count() == 2; }, 10000));
  scene.retired = true;
  QVERIFY(scene.destroyItem());
}
