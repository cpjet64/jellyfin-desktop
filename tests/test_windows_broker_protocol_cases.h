// Included in the adapter test's private namespace to share injected OS calls.
using HealthyModuleIdentity::Digest;
using HealthyModuleIdentity::NativeError;
using HealthyModuleIdentity::StageApi;
using HealthyModuleIdentity::StageLease;

Digest selectedDigest(const Digest&) { return HealthyModuleIdentity::expectedDigest(); }

std::shared_ptr<StageLease> stageFixture(QTemporaryDir& directory)
{
  CHECK(directory.isValid());
  QFile file(QDir(directory.path()).filePath(QStringLiteral("libmpv-2.dll")));
  CHECK(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
  CHECK(file.write("fixture", 7) == 7);
  file.close();
  const auto path = QDir::toNativeSeparators(directory.path()).toStdWString();
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  CHECK(handle != INVALID_HANDLE_VALUE);
  StageApi api; api.overrideDigest = selectedDigest;
  NativeError error = NativeError::Unavailable;
  auto lease = StageLease::acquireForTest(reinterpret_cast<std::uintptr_t>(handle), api, error);
  CHECK(CloseHandle(handle));
  CHECK(lease && error == NativeError::None);
  return std::shared_ptr<StageLease>(std::move(lease));
}

enum class AStream { Valid, Partial, EarlyEof, Trailing, Duplicate, MissingEof, WrongClosed };
enum class BStream { Valid, WrongRun, WrongLaunch, WrongGrant, WrongRevision, WrongDigest,
                     Partial, Unexpected, CancelRead, CancelPeek };
BStream bStream = BStream::Valid;
bool permitCurrent = true;

BOOL WINAPI duplicateParent(HANDLE, HANDLE, HANDLE, LPHANDLE out, DWORD, BOOL, DWORD)
{ *out = handle(9000); return TRUE; }
DWORD WINAPI fakePid() { return 19; }
DWORD WINAPI fakeProcessId(HANDLE) { return 19; }
BOOL WINAPI fakeHandleInfo(HANDLE, LPDWORD flags) { *flags = HANDLE_FLAG_INHERIT; return TRUE; }
BOOL WINAPI noQueuedBytes(HANDLE, LPVOID, DWORD, LPDWORD, LPDWORD available, LPDWORD)
{
  *available = 0;
  if (bStream == BStream::CancelPeek) permitCurrent = false;
  if (callbackBroker && callbackMode == 1) CHECK(callbackBroker->cancel() == Broker::Cancellation::Pending);
  if (callbackBroker && callbackMode == 2) callbackArmed = true;
  return TRUE;
}

WindowsRestartProcess* healthyProcess(const std::vector<WindowsEndpoint>& endpoints)
{
  std::vector<WindowsRestartChannel*> channels;
  std::vector<WindowsRestartIo*> io;
  for (auto endpoint : endpoints) { channels.push_back(endpoint.channel); io.push_back(endpoint.io); }
  auto api = processApi();
  api.duplicate = duplicateParent; api.currentProcess = current; api.currentProcessId = fakePid;
  api.processId = fakeProcessId; api.handleInfo = fakeHandleInfo;
  auto* result = WindowsRestartProcess::createHealthyWithChannels(api, channels, io, channels.back());
  CHECK(result); return result;
}

void bAcceptanceCase(WindowsBrokerOperations& operations, WindowsEndpoint grantEndpoint,
                     WindowsEndpoint ack, qint64 deadline)
{
  const Grant grant{1, 2, 3, QByteArray(64, 'a'), 3000, true};
  const auto frame = encodeGrant(grant);
  const auto digest = QCryptographicHash::hash(frame, QCryptographicHash::Sha256);
  auto result = operations.launchB(grant, digest, deadline);
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (result == Operations::Progress::Pending && std::chrono::steady_clock::now() < end) {
    std::this_thread::yield(); result = operations.launchB(grant, digest, deadline);
  }
  CHECK(result == Operations::Progress::Complete);
  CHECK(operations.transferToB(frame, deadline) == Operations::Progress::Complete);
  CHECK(grantEndpoint.channel->parentHandle() == INVALID_HANDLE_VALUE && lastWrite == frame);
  CHECK(operations.issueRestorePermit(deadline, [] { return permitCurrent; }) == Operations::Progress::Failed);
  scriptedPipe = ack.channel->parentHandle(); scriptedOffset = 0; scriptedChunk = 3;
  auto acceptance = GrantAccepted{1, 7, 2, 3, digest};
  if (bStream == BStream::WrongRun) ++acceptance.run;
  if (bStream == BStream::WrongLaunch) ++acceptance.launch;
  if (bStream == BStream::WrongGrant) ++acceptance.grant;
  if (bStream == BStream::WrongRevision) ++acceptance.revision;
  if (bStream == BStream::WrongDigest) acceptance.digest[0] = char(acceptance.digest.at(0) ^ 1);
  scriptedBytes = encodeGrantAccepted(acceptance);
  if (bStream == BStream::Partial) scriptedBytes.chop(1);
  auto accepted = operations.waitBGrantAccepted(deadline);
  while (accepted == Operations::Progress::Pending && std::chrono::steady_clock::now() < end)
    accepted = operations.waitBGrantAccepted(deadline);
  if (bStream == BStream::WrongRun || bStream == BStream::WrongLaunch || bStream == BStream::WrongGrant ||
      bStream == BStream::WrongRevision || bStream == BStream::WrongDigest || bStream == BStream::Partial) {
    CHECK(accepted == Operations::Progress::Failed);
    CHECK(operations.issueRestorePermit(deadline, [] { return permitCurrent; }) == Operations::Progress::Failed);
    return;
  }
  CHECK(accepted == Operations::Progress::Complete);
  permitCurrent = true;
  const auto beforePermit = lastWrite;
  const auto permit = operations.issueRestorePermit(deadline, [] { return permitCurrent; });
  if (bStream == BStream::CancelPeek) {
    CHECK(permit == Operations::Progress::Failed && lastWrite == beforePermit);
    return;
  }
  CHECK(permit == Operations::Progress::Complete);
  RestorePermit decoded;
  CHECK(decodeRestorePermit(lastWrite, {1, 7, 2, 3, 1, digest}, decoded));
  scriptedBytes = bStream == BStream::CancelRead ? QByteArray() : QByteArray(1, 'x');
  scriptedOffset = 0; scriptedPending = bStream == BStream::CancelRead;
  const auto monitor = operations.monitorB(deadline);
  if (bStream == BStream::CancelRead) {
    CHECK(monitor == Operations::Progress::Pending);
    scriptedRace = true;
    CHECK(operations.cancelBAndAwaitAck(deadline) == Operations::Progress::Failed);
    Control stop;
    CHECK(decodeControl(lastWrite, stop) && stop.kind == ControlKind::UserStop);
  } else CHECK(monitor == Operations::Progress::Failed);
}

void aStreamCase(AStream test)
{
  activeA = 0; aExited = false; waitUnknown = false;
  QTemporaryDir stage(QDir::currentPath() + "/w939-stage-XXXXXX");
  auto lease = stageFixture(stage);
  auto evidence = endpoint(), control = endpoint(), ack = endpoint();
  auto grant = endpoint(), bControl = endpoint(), bAck = endpoint(), identity = endpoint();
  auto* a = process({evidence, control, ack});
  auto* b = healthyProcess({grant, bControl, bAck, identity});
  launch(a);
  const int before = created;
  const Closed expected{1, 2, 1, 3, 4, 5, 6};
  WindowsBrokerOperations operations(*a, *b, evidence, control, ack, grant, bControl, bAck,
                                     L"C:\\fixture.exe", L"", 3, noQueuedBytes);
  CHECK(operations.configureHealthyTransfer(expected, 7, identity.channel, lease,
                                             [](const Grant&) { return true; }));
  scriptedPipe = ack.channel->parentHandle(); scriptedOffset = 0; scriptedChunk = 3;
  scriptedPending = test == AStream::MissingEof;
  scriptedBytes = encodeControl({ControlKind::Ack, 1, 3});
  if (test != AStream::EarlyEof) {
    Closed actual = expected;
    if (test == AStream::WrongClosed) ++actual.closure;
    auto closed = encodeClosed(actual);
    if (test == AStream::Partial) closed.chop(1);
    scriptedBytes += closed;
  }
  if (test == AStream::Trailing) scriptedBytes += 'x';
  if (test == AStream::Duplicate) scriptedBytes += encodeClosed(expected);
  const auto deadline = operations.nowMs() + 10000;
  CHECK(operations.internalReplacementStop(deadline) == Operations::Progress::Complete);
  const auto closed = operations.waitAClosed(deadline);
  if (test == AStream::EarlyEof || test == AStream::Partial || test == AStream::WrongClosed) {
    CHECK(closed == Operations::Progress::Failed);
  } else {
    CHECK(closed == Operations::Progress::Complete);
    aExited = true;
    bool forced = true;
    const auto settled = operations.waitAAndChildren(deadline, forced);
    CHECK(!forced);
    if (test == AStream::Valid) {
      CHECK(settled == Operations::Progress::Complete);
      bAcceptanceCase(operations, grant, bAck, deadline);
    }
    else if (test == AStream::MissingEof) CHECK(settled == Operations::Progress::Pending);
    else CHECK(settled == Operations::Progress::Failed);
  }
  if (test != AStream::Valid) {
    const Grant candidate{1, 2, 3, QByteArray(64, 'a'), 3000, true};
    const auto digest = QCryptographicHash::hash(encodeGrant(candidate), QCryptographicHash::Sha256);
    CHECK(operations.launchB(candidate, digest, deadline) == Operations::Progress::Failed);
    CHECK(operations.issueRestorePermit(deadline, [] { return permitCurrent; }) == Operations::Progress::Failed);
  }
  CHECK(created == before + (test == AStream::Valid ? 1 : 0));
  scriptedPending = false; scriptedRace = false; scriptedBuffer = nullptr;
  CHECK(operations.cleanupOwned(deadline) == Operations::Progress::Complete);
  scriptedPipe = INVALID_HANDLE_VALUE;
}

void retainedSettlementBlocksRelease()
{
  auto owned = endpoint();
  auto* owner = process({owned});
  scriptedPipe = owned.channel->parentHandle(); scriptedBytes = QByteArray(1, 'x');
  scriptedOffset = 0; scriptedChunk = 1; scriptedPending = false;
  CHECK(owned.io->beginTrackedRead(scriptedPipe, 1, now() + 10000) == IoProgress::Complete);
  const auto settlement = owned.io->settlement();
  CHECK(settlement && settlement->transferred == 1 && settlement->bytes[0] == 'x');
  CHECK(!owner->release());
  CHECK(owned.io->consumeSettlement(settlement->operation));
  CHECK(owner->release());
  scriptedPipe = INVALID_HANDLE_VALUE;
}

void exerciseBrokerCancel(bool duringGrant)
{
  activeA = 0; holdCreate = false; enteredCreate = false;
  QTemporaryDir stage(QDir::currentPath() + "/w939-cancel-XXXXXX");
  auto lease = stageFixture(stage);
  auto evidence = endpoint(), control = endpoint(), ack = endpoint();
  auto grant = endpoint(), bControl = endpoint(), bAck = endpoint(), identity = endpoint();
  auto* a = process({evidence, control, ack});
  auto* b = healthyProcess({grant, bControl, bAck, identity});
  launch(a);
  const int resumes = resumeCount;
  WindowsBrokerOperations operations(*a, *b, evidence, control, ack, grant, bControl, bAck,
                                     L"C:\\fixture.exe", L"", 3, noQueuedBytes);
  CHECK(operations.configureHealthyTransfer({1, 2, 1, 3, 4, 5, 6}, 7, identity.channel, lease,
                                             [](const Grant&) { return true; }));
  const Evidence source = sampleEvidence();
  Broker broker(operations, evidence.channel, source.grant);
  CHECK(broker.capture(evidence.channel, source) && broker.startReplacement());
  scriptedPipe = ack.channel->parentHandle(); scriptedOffset = 0; scriptedChunk = 3;
  scriptedPending = false;
  scriptedBytes = encodeControl({ControlKind::Ack, 1, 3}) + encodeClosed({1, 2, 1, 3, 4, 5, 6});
  aExited = true;
  heldGrant = grant.channel->parentHandle(); holdGrant = duringGrant;
  cancelGrant = false; delayGrantCancel = duringGrant;
  enteredCreate = false; holdCreate = !duringGrant;
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!(duringGrant ? grant.io->pending() : enteredCreate.load()) && !broker.failed() &&
         std::chrono::steady_clock::now() < end) { broker.step(); std::this_thread::yield(); }
  CHECK((duringGrant ? grant.io->pending() : enteredCreate.load()) && broker.burned());
  CHECK(broker.cancel() == Broker::Cancellation::Pending);
  broker.step();
  if (duringGrant) {
    CHECK(cancelGrant && broker.cancellation() == Broker::Cancellation::Acknowledged);
    broker.step(); CHECK(!broker.failed() && grant.io->pending());
    delayGrantCancel = false;
  } else {
    CHECK(broker.cancellation() == Broker::Cancellation::Pending);
    holdCreate = false;
    while (broker.cancellation() == Broker::Cancellation::Pending && std::chrono::steady_clock::now() < end) {
      broker.step(); std::this_thread::yield();
    }
    CHECK(broker.cancellation() == Broker::Cancellation::Unconfirmed && resumeCount == resumes);
  }
  while (!broker.failed() && !broker.unconfirmed() && std::chrono::steady_clock::now() < end) broker.step();
  CHECK(broker.failed());
  heldGrant = INVALID_HANDLE_VALUE; holdGrant = false; scriptedPipe = INVALID_HANDLE_VALUE;
}

void exercisePermitClaim(int mode)
{
  activeA = 0; callbackMode = mode; callbackArmed = false; callbackIntent = true;
  permitWrites = stopWrites = 0;
  QTemporaryDir stage(QDir::currentPath() + "/w967-claim-XXXXXX");
  auto lease = stageFixture(stage);
  auto evidence = endpoint(), control = endpoint(), ack = endpoint();
  auto grant = endpoint(), bControl = endpoint(), bAck = endpoint(), identity = endpoint();
  auto* a = process({evidence, control, ack});
  auto* b = healthyProcess({grant, bControl, bAck, identity});
  launch(a);
  WindowsBrokerOperations operations(*a, *b, evidence, control, ack, grant, bControl, bAck,
                                     L"C:\\fixture.exe", L"", 3, noQueuedBytes);
  CHECK(operations.configureHealthyTransfer({1, 2, 1, 3, 4, 5, 6}, 7, identity.channel, lease,
                                             [](const Grant&) { return callbackIntent; }));
  const Evidence source = sampleEvidence();
  Broker broker(operations, evidence.channel, source.grant);
  CHECK(broker.capture(evidence.channel, source) && broker.startReplacement());
  callbackBroker = &broker; callbackControl = bControl.channel->parentHandle();
  callbackAck = bAck.channel->parentHandle();
  scriptedPipe = ack.channel->parentHandle(); scriptedOffset = 0; scriptedChunk = 3;
  scriptedBytes = encodeControl({ControlKind::Ack, 1, 3}) + encodeClosed({1, 2, 1, 3, 4, 5, 6});
  scriptedPending = false; aExited = true;
  bool acceptanceReady = false;
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!broker.failed() && !broker.unconfirmed() && !broker.completed() && std::chrono::steady_clock::now() < end) {
    broker.step();
    if (!acceptanceReady && grant.channel->parentHandle() == INVALID_HANDLE_VALUE) {
      acceptanceReady = true; scriptedPipe = callbackAck; scriptedOffset = 0;
      const auto digest = QCryptographicHash::hash(encodeGrant(source.grant), QCryptographicHash::Sha256);
      scriptedBytes = encodeGrantAccepted({1, 7, 2, 3, digest});
    }
    std::this_thread::yield();
  }
  CHECK(acceptanceReady && !broker.completed());
  CHECK(permitWrites == (mode == 1 ? 0 : 1));
  CHECK(stopWrites == 1 && broker.cancellation() != Broker::Cancellation::Local);
  CHECK(broker.failed() || broker.unconfirmed());
  callbackIntent = true;
  broker.step();
  CHECK(!broker.completed() && permitWrites == (mode == 1 ? 0 : 1) && stopWrites == 1);
  callbackBroker = nullptr; callbackControl = callbackAck = INVALID_HANDLE_VALUE;
  callbackMode = 0; scriptedPipe = INVALID_HANDLE_VALUE;
}

void adapterProtocolCases()
{
  for (int mode : {1, 2, 3}) exercisePermitClaim(mode);
  retainedSettlementBlocksRelease();
  for (auto test : {AStream::Valid, AStream::Partial, AStream::EarlyEof,
                    AStream::Trailing, AStream::Duplicate, AStream::MissingEof,
                    AStream::WrongClosed}) aStreamCase(test);
  for (auto test : {BStream::WrongRun, BStream::WrongLaunch, BStream::WrongGrant,
                    BStream::WrongRevision, BStream::WrongDigest, BStream::Partial,
                    BStream::Unexpected, BStream::CancelRead, BStream::CancelPeek}) {
    bStream = test;
    aStreamCase(AStream::Valid);
  }
}
