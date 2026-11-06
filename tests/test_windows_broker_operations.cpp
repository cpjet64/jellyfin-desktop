#include "fixture_restart/WindowsBrokerOperations.h"
#include "fixture_restart/WindowsRestartParent.h"
#include "player/PlaybackCheckpoint.h"
#ifdef _WIN32
#include <QCoreApplication>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>
#define CHECK(value) do { if (!(value)) std::abort(); } while (false)
using namespace FixtureRestart;

namespace {
constexpr quintptr base = 100;
int nextHandle = 0, created = 0, closedGrant = 0, activeA = 1;
DWORD errorCode = 0;
QByteArray lastWrite;
HANDLE heldGrant = INVALID_HANDLE_VALUE;
bool holdGrant = false, cancelGrant = false, delayGrantCancel = false;
HANDLE evidencePipe = INVALID_HANDLE_VALUE;
QByteArray evidenceBytes;
qsizetype evidenceOffset = 0;
bool badAck = false;
bool aExited = false, waitUnknown = false;
int afterEvidence = 0;
HANDLE aProcess = INVALID_HANDLE_VALUE;
std::atomic<bool> holdCreate{false}, enteredCreate{false};
std::atomic<int> resumeCount{0};
HANDLE handle(int n) { return reinterpret_cast<HANDLE>(base + quintptr(n)); }
HANDLE WINAPI current() { return handle(90); }
BOOL WINAPI openToken(HANDLE, DWORD, PHANDLE out) { *out = handle(91); return TRUE; }
BOOL WINAPI tokenInfo(HANDLE, TOKEN_INFORMATION_CLASS, LPVOID out, DWORD, PDWORD size)
{ *size = sizeof(TOKEN_USER); if (!out) return FALSE; static_cast<TOKEN_USER*>(out)->User.Sid = reinterpret_cast<PSID>(1); return TRUE; }
BOOL WINAPI sidString(PSID, LPWSTR* out) { static wchar_t sid[] = L"S-1-5-21-1"; *out = sid; return TRUE; }
BOOL WINAPI descriptor(LPCWSTR, DWORD, PSECURITY_DESCRIPTOR* out, PULONG) { *out = reinterpret_cast<PSECURITY_DESCRIPTOR>(1); return TRUE; }
HLOCAL WINAPI localFree(HLOCAL) { return nullptr; }
NTSTATUS WINAPI random(BCRYPT_ALG_HANDLE, PUCHAR bytes, ULONG n, ULONG) { std::memset(bytes, 7, n); return 0; }
HANDLE WINAPI pipe(LPCWSTR, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPSECURITY_ATTRIBUTES) { return handle(++nextHandle); }
HANDLE WINAPI file(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) { return handle(++nextHandle); }
HANDLE WINAPI event(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR) { return handle(++nextHandle); }
BOOL WINAPI connect(HANDLE, LPOVERLAPPED) { errorCode = ERROR_IO_PENDING; return FALSE; }
BOOL WINAPI ioResult(HANDLE h, LPOVERLAPPED, LPDWORD count, BOOL) {
  if (h == heldGrant && holdGrant) {
    errorCode = cancelGrant && !delayGrantCancel ? ERROR_OPERATION_ABORTED : ERROR_IO_INCOMPLETE;
    return FALSE;
  }
  *count = 0; return TRUE;
}
BOOL WINAPI cancelIo(HANDLE h, LPOVERLAPPED) { if (h == heldGrant) cancelGrant = true; return TRUE; }
BOOL WINAPI closeHandle(HANDLE h) { if (h == handle(10)) ++closedGrant; return TRUE; }
DWORD WINAPI lastError() { return errorCode; }
ULONGLONG WINAPI now() { return GetTickCount64(); }
BOOL WINAPI resetEvent(HANDLE) { return TRUE; }
BOOL WINAPI writeFile(HANDLE h, LPCVOID data, DWORD size, LPDWORD count, LPOVERLAPPED)
{ if (h == heldGrant && holdGrant) { errorCode = ERROR_IO_PENDING; return FALSE; }
  lastWrite = QByteArray(static_cast<const char*>(data), size); *count = size; return TRUE; }
BOOL WINAPI readFile(HANDLE h, LPVOID data, DWORD size, LPDWORD count, LPOVERLAPPED)
{
  if (h == evidencePipe) {
    if (evidenceOffset == evidenceBytes.size()) {
      if (afterEvidence == 1) aExited = true;
      if (afterEvidence == 2) waitUnknown = true;
      errorCode = ERROR_BROKEN_PIPE; return FALSE;
    }
    const auto length = std::min<qsizetype>(size, evidenceBytes.size() - evidenceOffset);
    std::memcpy(data, evidenceBytes.constData() + evidenceOffset, size_t(length));
    evidenceOffset += length; *count = DWORD(length); return TRUE;
  }
  CHECK(size == 24);
  Control request;
  CHECK(decodeControl(lastWrite, request));
  if (request.kind == ControlKind::InternalStop && activeA == 0) aExited = true;
  const QByteArray ack = encodeControl({ControlKind::Ack, request.sequence, request.revision + (badAck ? 1 : 0)});
  std::memcpy(data, ack.constData(), size); *count = size; return TRUE;
}
HANDLE WINAPI createJob(LPSECURITY_ATTRIBUTES, LPCWSTR) { return handle(80 + ++created); }
BOOL WINAPI setJob(HANDLE, JOBOBJECTINFOCLASS, LPVOID, DWORD) { return TRUE; }
BOOL WINAPI init(LPPROC_THREAD_ATTRIBUTE_LIST list, DWORD, DWORD, PSIZE_T size) { *size = 256; return list != nullptr; }
BOOL WINAPI update(LPPROC_THREAD_ATTRIBUTE_LIST, DWORD, DWORD_PTR, PVOID, SIZE_T, PVOID, PSIZE_T) { return TRUE; }
void WINAPI deleteAttrs(LPPROC_THREAD_ATTRIBUTE_LIST) {}
BOOL WINAPI createProcess(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL,
    DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION out)
{
  enteredCreate = true;
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (holdCreate && std::chrono::steady_clock::now() < end) std::this_thread::yield();
  if (holdCreate) return FALSE;
  out->hProcess = handle(70 + created); out->hThread = handle(60 + created); return TRUE;
}
BOOL WINAPI inJob(HANDLE, HANDLE, PBOOL result) { *result = TRUE; return TRUE; }
DWORD WINAPI resume(HANDLE) { ++resumeCount; return 1; }
BOOL WINAPI terminate(HANDLE, UINT) { activeA = 0; aExited = true; return TRUE; }
DWORD WINAPI wait(HANDLE h, DWORD) {
  if (h == aProcess && waitUnknown) return WAIT_FAILED;
  return h == aProcess && !aExited ? WAIT_TIMEOUT : WAIT_OBJECT_0;
}
BOOL WINAPI query(HANDLE, JOBOBJECTINFOCLASS, LPVOID out, DWORD, LPDWORD)
{ static_cast<JOBOBJECT_BASIC_ACCOUNTING_INFORMATION*>(out)->ActiveProcesses = DWORD(activeA); return TRUE; }
BOOL WINAPI exitCode(HANDLE, LPDWORD out) { *out = 0; return TRUE; }

WindowsChannelApi channelApi()
{
  WindowsChannelApi a; a.currentProcess = current; a.openToken = openToken; a.tokenInfo = tokenInfo;
  a.sidString = sidString; a.descriptor = descriptor; a.localFree = localFree; a.random = random;
  a.createPipe = pipe; a.createFile = file; a.event = event; a.connect = connect; a.result = ioResult;
  a.cancel = cancelIo; a.close = closeHandle; a.error = lastError; a.now = now; return a;
}
WindowsIoApi ioApi()
{
  WindowsIoApi a; a.createEvent = event; a.closeHandle = closeHandle; a.resetEvent = resetEvent;
  a.readFile = readFile; a.writeFile = writeFile; a.result = ioResult; a.cancel = cancelIo;
  a.error = lastError; a.now = now; return a;
}
WindowsProcessApi processApi()
{
  WindowsProcessApi a; a.createJob = createJob; a.setJob = setJob; a.initAttributes = init;
  a.updateAttribute = update; a.deleteAttributes = deleteAttrs; a.createProcess = createProcess;
  a.inJob = inJob; a.resume = resume; a.terminateJob = terminate; a.terminateProcess = terminate;
  a.wait = wait; a.queryJob = query; a.exitCode = exitCode; a.close = closeHandle; a.now = now; return a;
}
WindowsEndpoint endpoint()
{
  auto* channel = WindowsRestartChannel::create(channelApi());
  const auto result = channel->begin(now() + 10000);
  CHECK(result == ChannelProgress::Complete || (result == ChannelProgress::Pending && channel->poll() == ChannelProgress::Complete));
  return {channel, new WindowsRestartIo(ioApi())};
}
WindowsRestartProcess* process(const std::vector<WindowsEndpoint>& endpoints)
{
  std::vector<WindowsRestartChannel*> channels;
  std::vector<WindowsRestartIo*> io;
  for (auto e : endpoints) { channels.push_back(e.channel); io.push_back(e.io); }
  auto* result = WindowsRestartProcess::createWithChannels(processApi(), channels, io);
  CHECK(result);
  return result;
}
void launch(WindowsRestartProcess* process)
{
  aExited = false; waitUnknown = false;
  CHECK(process->launch(L"C:\\fixture.exe", L"", now() + 10000) == ProcessProgress::Pending);
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < end) {
    const auto result = process->pollLaunch();
    if (result == ProcessProgress::Complete) { aProcess = handle(70 + created); return; }
    CHECK(result == ProcessProgress::Pending); std::this_thread::yield();
  }
  std::abort();
}
void ownedOperations()
{
  auto evidence = endpoint(), aControl = endpoint(), aAck = endpoint();
  auto bGrant = endpoint(), bControl = endpoint(), bAck = endpoint();
  auto* a = process({evidence, aControl, aAck});
  auto* b = process({bGrant, bControl, bAck});
  launch(a);
  WindowsBrokerOperations operations(*a, *b, evidence, aControl, aAck, bGrant, bControl, bAck,
                                     L"C:\\fixture.exe", L"", 3);
  CHECK(operations.ownsAChannel(evidence.channel));
  CHECK(!operations.ownsAChannel(bGrant.channel));
  const qint64 deadline = operations.nowMs() + 10000;
  CHECK(operations.internalReplacementStop(deadline) == Operations::Progress::Complete);
  Control sent;
  CHECK(decodeControl(lastWrite, sent) && sent.kind == ControlKind::InternalStop);
  bool forced = true;
  CHECK(operations.waitAAndChildren(deadline, forced) == Operations::Progress::Pending && !forced);
  activeA = 0; aExited = true;
  CHECK(operations.waitAAndChildren(deadline, forced) == Operations::Progress::Complete && !forced);
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  auto progress = operations.launchB(deadline);
  while (progress == Operations::Progress::Pending && std::chrono::steady_clock::now() < end) {
    std::this_thread::yield();
    progress = operations.launchB(deadline);
  }
  CHECK(progress == Operations::Progress::Complete);
  const Grant grant{1, 2, 3, QByteArray(64, 'a'), 3000, true};
  const QByteArray frame = encodeGrant(grant);
  CHECK(operations.transferToB(frame, deadline) == Operations::Progress::Complete);
  CHECK(bGrant.channel->parentHandle() == INVALID_HANDLE_VALUE && lastWrite == frame);
  CHECK(operations.cancelBAndAwaitAck(deadline) == Operations::Progress::Complete);
  CHECK(decodeControl(lastWrite, sent) && sent.kind == ControlKind::UserStop && sent.revision == 4);
  CHECK(operations.cleanupOwned(deadline) == Operations::Progress::Complete);
}

Evidence sampleEvidence()
{
  Evidence evidence;
  evidence.receipt = R"({"attempt":"42","core":"4","entry":"5","host":"1","revision":"3"})";
  evidence.observation = R"({"id":"7","paused":true,"positionMs":"3000","receivedNs":"200","requestedNs":"100"})";
  PlaybackCheckpoint checkpoint;
  checkpoint.intent.epoch = 1; checkpoint.intent.request = 2; checkpoint.intent.revision = 3;
  checkpoint.intent.desired = PlaybackIntent::Desired::Paused; checkpoint.intent.timelineQualified = true;
  checkpoint.positionKnown = true; checkpoint.positionMs = 3000;
  checkpoint.item = QStringLiteral("fixture-clip"); checkpoint.source = QStringLiteral("fixture-source");
  CHECK(PlaybackCheckpointCodec::encode(checkpoint, evidence.checkpoint));
  evidence.sourceAttempt = 42;
  evidence.grant = {1, 2, 3, QByteArray(64, 'a'), 3000, true};
  return evidence;
}

void actualBrokerPath()
{
  auto evidenceChannel = endpoint(), aControl = endpoint(), aAck = endpoint();
  auto bGrant = endpoint(), bControl = endpoint(), bAck = endpoint();
  auto* a = process({evidenceChannel, aControl, aAck});
  auto* b = process({bGrant, bControl, bAck});
  launch(a);
  WindowsBrokerOperations operations(*a, *b, evidenceChannel, aControl, aAck, bGrant, bControl, bAck,
                                     L"C:\\fixture.exe", L"", 3);
  const Evidence evidence = sampleEvidence();
  Broker broker(operations, evidenceChannel.channel, evidence.grant);
  CHECK(broker.capture(evidenceChannel.channel, evidence));
  CHECK(broker.startReplacement());
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!broker.completed() && !broker.failed() && std::chrono::steady_clock::now() < end) {
    broker.step(); std::this_thread::yield();
  }
  CHECK(broker.completed() && broker.burned());
  CHECK(broker.cancel() == Broker::Cancellation::Pending);
  broker.step();
  CHECK(broker.cancellation() == Broker::Cancellation::Acknowledged);
  broker.step();
  CHECK(broker.failed());
}

void cancelBeforeASettled()
{
  activeA = 1;
  auto evidenceChannel = endpoint(), aControl = endpoint(), aAck = endpoint();
  auto bGrant = endpoint(), bControl = endpoint(), bAck = endpoint();
  auto* a = process({evidenceChannel, aControl, aAck});
  auto* b = process({bGrant, bControl, bAck});
  launch(a);
  const int before = created;
  WindowsBrokerOperations operations(*a, *b, evidenceChannel, aControl, aAck, bGrant, bControl, bAck,
                                     L"C:\\fixture.exe", L"", 3);
  const Evidence evidence = sampleEvidence();
  Broker broker(operations, evidenceChannel.channel, evidence.grant);
  CHECK(broker.capture(evidenceChannel.channel, evidence) && broker.startReplacement());
  broker.step(); broker.step();
  CHECK(!broker.completed() && created == before);
  CHECK(broker.cancel() == Broker::Cancellation::Local);
  broker.step();
  CHECK(broker.failed() && created == before);
}

void cancelDuringPendingGrant()
{
  activeA = 0;
  auto evidenceChannel = endpoint(), aControl = endpoint(), aAck = endpoint();
  auto bGrant = endpoint(), bControl = endpoint(), bAck = endpoint();
  heldGrant = bGrant.channel->parentHandle();
  holdGrant = true; cancelGrant = false; delayGrantCancel = true;
  auto* a = process({evidenceChannel, aControl, aAck});
  auto* b = process({bGrant, bControl, bAck});
  launch(a);
  WindowsBrokerOperations operations(*a, *b, evidenceChannel, aControl, aAck, bGrant, bControl, bAck,
                                     L"C:\\fixture.exe", L"", 3);
  const Evidence evidence = sampleEvidence();
  Broker broker(operations, evidenceChannel.channel, evidence.grant);
  CHECK(broker.capture(evidenceChannel.channel, evidence) && broker.startReplacement());
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!bGrant.io->pending() && !broker.failed() && std::chrono::steady_clock::now() < end) {
    broker.step(); std::this_thread::yield();
  }
  CHECK(bGrant.io->pending() && broker.burned());
  CHECK(broker.cancel() == Broker::Cancellation::Pending);
  broker.step();
  CHECK(cancelGrant && broker.cancellation() == Broker::Cancellation::Acknowledged);
  broker.step();
  CHECK(!broker.failed() && bGrant.io->pending());
  delayGrantCancel = false;
  broker.step();
  CHECK(broker.failed());
  heldGrant = INVALID_HANDLE_VALUE; holdGrant = false;
}

void wrongAcknowledgement()
{
  activeA = 0; badAck = true;
  auto evidenceChannel = endpoint(), aControl = endpoint(), aAck = endpoint();
  auto bGrant = endpoint(), bControl = endpoint(), bAck = endpoint();
  auto* a = process({evidenceChannel, aControl, aAck});
  auto* b = process({bGrant, bControl, bAck});
  launch(a);
  const int before = created;
  WindowsBrokerOperations operations(*a, *b, evidenceChannel, aControl, aAck, bGrant, bControl, bAck,
                                     L"C:\\fixture.exe", L"", 3);
  const Evidence evidence = sampleEvidence();
  Broker broker(operations, evidenceChannel.channel, evidence.grant);
  CHECK(broker.capture(evidenceChannel.channel, evidence) && broker.startReplacement());
  broker.step(); broker.step();
  CHECK(broker.failed() && created == before);
  badAck = false;
}

void cancelDuringLaunch()
{
  activeA = 0; holdCreate = false; enteredCreate = false;
  auto evidenceChannel = endpoint(), aControl = endpoint(), aAck = endpoint();
  auto bGrant = endpoint(), bControl = endpoint(), bAck = endpoint();
  auto* a = process({evidenceChannel, aControl, aAck});
  auto* b = process({bGrant, bControl, bAck});
  launch(a);
  const int before = resumeCount;
  enteredCreate = false; holdCreate = true;
  WindowsBrokerOperations operations(*a, *b, evidenceChannel, aControl, aAck, bGrant, bControl, bAck,
                                     L"C:\\fixture.exe", L"", 3);
  const Evidence evidence = sampleEvidence();
  Broker broker(operations, evidenceChannel.channel, evidence.grant);
  CHECK(broker.capture(evidenceChannel.channel, evidence) && broker.startReplacement());
  broker.step(); broker.step(); broker.step();
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!enteredCreate && std::chrono::steady_clock::now() < end) std::this_thread::yield();
  CHECK(enteredCreate && broker.burned());
  CHECK(broker.cancel() == Broker::Cancellation::Pending);
  broker.step();
  CHECK(broker.cancellation() == Broker::Cancellation::Pending);
  holdCreate = false;
  while (broker.cancellation() == Broker::Cancellation::Pending && std::chrono::steady_clock::now() < end) {
    broker.step(); std::this_thread::yield();
  }
  CHECK(broker.cancellation() == Broker::Cancellation::Unconfirmed && resumeCount == before);
  broker.step();
  CHECK(broker.failed());
}

void rejectsMismatchedEndpoint()
{
  auto evidence = endpoint(), aControl = endpoint(), aAck = endpoint();
  auto bGrant = endpoint(), bControl = endpoint(), bAck = endpoint();
  auto* a = process({evidence, aControl, aAck});
  auto* b = process({bGrant, bControl, bAck});
  const WindowsEndpoint mismatched{aControl.channel, bAck.io};
  WindowsBrokerOperations operations(*a, *b, evidence, mismatched, aAck, bGrant, bControl, bAck,
                                     L"C:\\fixture.exe", L"", 3);
  CHECK(!operations.ownsAChannel(evidence.channel));
  CHECK(operations.internalReplacementStop(operations.nowMs() + 10000) == Operations::Progress::Failed);
  CHECK(operations.cleanupOwned(operations.nowMs() + 10000) == Operations::Progress::Failed);
  CHECK(a->release() && b->release());
}

void actualParentPath(bool corrupt)
{
  activeA = 0;
  auto evidence = endpoint(), aControl = endpoint(), aAck = endpoint();
  auto bGrant = endpoint(), bControl = endpoint(), bAck = endpoint();
  evidencePipe = evidence.channel->parentHandle();
  const Evidence source = sampleEvidence();
  evidenceBytes = encodeEvidence(source);
  CHECK(!evidenceBytes.isEmpty());
  if (corrupt) evidenceBytes.append('x');
  evidenceOffset = 0;
  auto* a = process({evidence, aControl, aAck});
  auto* b = process({bGrant, bControl, bAck});
  launch(a);
  const int before = created;
  WindowsBrokerOperations operations(*a, *b, evidence, aControl, aAck, bGrant, bControl, bAck,
                                     L"C:\\fixture.exe", L"", 3);
  WindowsRestartParent parent(operations, evidence, source.grant);
  const bool started = parent.begin();
  CHECK(started != corrupt);
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (parent.state() != WindowsRestartParent::State::Transferred &&
         parent.state() != WindowsRestartParent::State::Failed && std::chrono::steady_clock::now() < end) {
    parent.step(); std::this_thread::yield();
  }
  if (corrupt) {
    CHECK(parent.state() == WindowsRestartParent::State::Failed && created == before);
  } else {
    CHECK(parent.state() == WindowsRestartParent::State::Transferred && created == before + 1);
    CHECK(parent.frozenEvidence().receipt == source.receipt);
    CHECK(parent.cancel() == Broker::Cancellation::Pending);
    parent.step(); parent.step();
    CHECK(parent.state() == WindowsRestartParent::State::Failed);
  }
  evidencePipe = INVALID_HANDLE_VALUE;
}

void parentRejectsDeadOrUnknownA(bool unknown)
{
  activeA = 0;
  auto evidence = endpoint(), aControl = endpoint(), aAck = endpoint();
  auto bGrant = endpoint(), bControl = endpoint(), bAck = endpoint();
  auto* a = process({evidence, aControl, aAck});
  auto* b = process({bGrant, bControl, bAck});
  launch(a);
  const int before = created;
  WindowsBrokerOperations operations(*a, *b, evidence, aControl, aAck, bGrant, bControl, bAck,
                                     L"C:\\fixture.exe", L"", 3);
  const Evidence source = sampleEvidence();
  WindowsRestartParent parent(operations, evidence, source.grant);
  aExited = !unknown; waitUnknown = unknown;
  CHECK(!parent.begin() && parent.failure() == WindowsRestartParent::Failure::Channel);
  waitUnknown = false; aExited = true;
  parent.step();
  CHECK(parent.state() == WindowsRestartParent::State::Failed && created == before);
}

void parentRejectsLostAAfterRead(bool unknown)
{
  activeA = 0;
  auto evidence = endpoint(), aControl = endpoint(), aAck = endpoint();
  auto bGrant = endpoint(), bControl = endpoint(), bAck = endpoint();
  evidencePipe = evidence.channel->parentHandle();
  const Evidence source = sampleEvidence();
  evidenceBytes = encodeEvidence(source); evidenceOffset = 0;
  auto* a = process({evidence, aControl, aAck});
  auto* b = process({bGrant, bControl, bAck});
  launch(a);
  const int before = created;
  WindowsBrokerOperations operations(*a, *b, evidence, aControl, aAck, bGrant, bControl, bAck,
                                     L"C:\\fixture.exe", L"", 3);
  WindowsRestartParent parent(operations, evidence, source.grant);
  afterEvidence = unknown ? 2 : 1;
  CHECK(!parent.begin() && parent.failure() == WindowsRestartParent::Failure::Evidence);
  afterEvidence = 0; waitUnknown = false; aExited = true;
  parent.step();
  CHECK(parent.state() == WindowsRestartParent::State::Failed && created == before);
  evidencePipe = INVALID_HANDLE_VALUE;
}
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  ownedOperations(); actualBrokerPath(); cancelBeforeASettled(); cancelDuringPendingGrant();
  wrongAcknowledgement(); cancelDuringLaunch();
  rejectsMismatchedEndpoint();
  actualParentPath(false); actualParentPath(true);
  parentRejectsDeadOrUnknownA(false); parentRejectsDeadOrUnknownA(true);
  parentRejectsLostAAfterRead(false); parentRejectsLostAAfterRead(true);
  return 0;
}
#else
int main() { return 0; }
#endif
