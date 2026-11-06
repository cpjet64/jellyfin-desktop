enum class V2Case { BeforeAck, AfterAck, WrongHost, WrongCore, WrongIntent, WrongAttempt, NoArm, NoArmAfterV2,
                    DuplicateV2, Trailing, EarlyEof, PartialRace };

struct V2Fixture {
  QTemporaryDir stage{QDir::currentPath() + "/w981-stage-XXXXXX"};
  std::shared_ptr<StageLease> lease = stageFixture(stage);
  WindowsEndpoint evidence = endpoint(), control = endpoint(), ack = endpoint();
  WindowsEndpoint grant = endpoint(), bControl = endpoint(), bAck = endpoint(), identity = endpoint();
  WindowsRestartProcess* a = process({evidence, control, ack});
  WindowsRestartProcess* b = healthyProcess({grant, bControl, bAck, identity});
  WindowsBrokerOperations operations{*a, *b, evidence, control, ack, grant, bControl, bAck,
                                     L"C:\\fixture.exe", L"", 3, noQueuedBytes};
  Grant binding{1, 2, 3, QByteArray(64, 'a'), 3000, true};
  QByteArray frame = encodeGrant(binding);
  QByteArray digest = QCryptographicHash::hash(frame, QCryptographicHash::Sha256);
  qint64 deadline = operations.nowMs() + 10000;

  void prepare();
  void finish();
};

void V2Fixture::prepare()
{
  activeA = 0; launch(a);
  CHECK(operations.configureHealthyTransfer({1, 2, 1, 3, 4, 5, 6}, 7, identity.channel, lease,
                                             [](const Grant&) { return true; }));
  scriptedPipe = ack.channel->parentHandle(); scriptedOffset = 0; scriptedChunk = 3;
  scriptedBytes = encodeControl({ControlKind::Ack, 1, 3}) + encodeClosed({1, 2, 1, 3, 4, 5, 6});
  scriptedPending = false; scriptedPauseAt = -1;
  CHECK(operations.internalReplacementStop(deadline) == Operations::Progress::Complete);
  CHECK(operations.waitAClosed(deadline) == Operations::Progress::Complete);
  aExited = true; bool forced = true;
  CHECK(operations.waitAAndChildren(deadline, forced) == Operations::Progress::Complete && !forced);
  auto status = operations.launchB(binding, digest, deadline);
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (status == Operations::Progress::Pending && std::chrono::steady_clock::now() < end) {
    std::this_thread::yield(); status = operations.launchB(binding, digest, deadline);
  }
  CHECK(status == Operations::Progress::Complete);
  CHECK(operations.transferToB(frame, deadline) == Operations::Progress::Complete);
  scriptedPipe = bAck.channel->parentHandle(); scriptedOffset = 0; scriptedChunk = 3;
}

void V2Fixture::finish()
{
  scriptedPending = false; scriptedRace = false; scriptedPauseAt = -1; scriptedBuffer = nullptr;
  CHECK(operations.cleanupOwned(deadline) == Operations::Progress::Complete);
  scriptedPipe = INVALID_HANDLE_VALUE;
}

QByteArray v2CancellationStream(V2Case test, const QByteArray& digest)
{
  const QByteArray acceptance = encodeGrantAccepted({1, 7, 2, 3, digest, 10, 11, 12, 13});
  const QByteArray control = encodeControl({ControlKind::Ack, 2, 4});
  const QByteArray noArm = encodeCancelNoArm({1, 7, 4, 2, digest});
  CancelResult terminal{1, 7, 2, 3, 2, 10, 11, 12, 13, CancelOutcome::NoLoad, CancelFailure::None};
  if (test == V2Case::WrongHost) ++terminal.host;
  if (test == V2Case::WrongCore) ++terminal.core;
  if (test == V2Case::WrongIntent) ++terminal.intentRevision;
  if (test == V2Case::WrongAttempt) ++terminal.attempt;
  if (test == V2Case::NoArm) return control + noArm;
  if (test == V2Case::NoArmAfterV2) return acceptance + control + noArm;
  if (test == V2Case::DuplicateV2) return acceptance + acceptance + control + encodeCancelResult(terminal);
  QByteArray stream = test == V2Case::AfterAck ? control + acceptance : acceptance + control;
  stream += encodeCancelResult(terminal);
  if (test == V2Case::Trailing) stream += 'x';
  if (test == V2Case::EarlyEof) stream.chop(1);
  return stream;
}

void v2CancellationCase(V2Case test)
{
  V2Fixture fixture; fixture.prepare();
  scriptedBytes = v2CancellationStream(test, fixture.digest); scriptedOffset = 0;
  if (test == V2Case::PartialRace) {
    scriptedPauseAt = 7;
    auto result = fixture.operations.waitBGrantAccepted(fixture.deadline);
    while (result == Operations::Progress::Pending && !fixture.bAck.io->pending())
      result = fixture.operations.waitBGrantAccepted(fixture.deadline);
    CHECK(result == Operations::Progress::Pending && fixture.bAck.io->pending());
    scriptedRace = true;
  }
  auto status = fixture.operations.cancelBAndAwaitAck(fixture.deadline);
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (status == Operations::Progress::Pending && std::chrono::steady_clock::now() < end)
    status = fixture.operations.cancelBAndAwaitAck(fixture.deadline);
  const bool accepted = test == V2Case::BeforeAck || test == V2Case::AfterAck ||
                        test == V2Case::NoArm || test == V2Case::PartialRace;
  if (status != (accepted ? Operations::Progress::Complete : Operations::Progress::Failed))
    std::fprintf(stderr, "V2 case %d returned %d, offset %lld of %lld\n", int(test), int(status),
                 static_cast<long long>(scriptedOffset), static_cast<long long>(scriptedBytes.size()));
  CHECK(status == (accepted ? Operations::Progress::Complete : Operations::Progress::Failed));
  CHECK(scriptedOffset <= scriptedBytes.size());
  if (accepted) CHECK(scriptedOffset == scriptedBytes.size());
  fixture.finish();
}

void v2AdapterCases()
{
  for (auto test : {V2Case::BeforeAck, V2Case::AfterAck, V2Case::WrongHost, V2Case::WrongCore,
                    V2Case::WrongIntent, V2Case::WrongAttempt, V2Case::NoArm,
                    V2Case::NoArmAfterV2, V2Case::DuplicateV2, V2Case::Trailing,
                    V2Case::EarlyEof, V2Case::PartialRace}) v2CancellationCase(test);
}
