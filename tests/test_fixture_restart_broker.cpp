#include "fixture_restart/RestartBroker.h"
#include "player/PlaybackCheckpoint.h"
#include <QCoreApplication>
#include <QtEndian>
#include <cstdlib>
#define CHECK(condition) do { if (!(condition)) std::abort(); } while (false)

using namespace FixtureRestart;

struct Fake : Operations {
  qint64 clock = 100;
  const void* channel = this;
  bool held = true, evidenceValid = true, stopped = true, exited = true, forced = false;
  bool launched = true, transferred = true;
  bool ack = true;
  bool cancelDuringLaunch = false, cancelDuringTransfer = false, lateAck = false;
  bool holdTransfer = false, holdAck = false;
  Broker* target = nullptr;
  int stopCalls = 0, waitCalls = 0, launchCalls = 0, transferCalls = 0;
  QByteArray frame;
  qint64 nowMs() const override { return clock; }
  bool ownsAChannel(const void* candidate) const override { return held && candidate == channel; }
  bool validateEvidence(const Evidence&) const override { return evidenceValid; }
  Progress internalReplacementStop(qint64 deadline) override { ++stopCalls; CHECK(deadline == 150100); return stopped ? Progress::Complete : Progress::Failed; }
  Progress waitAAndChildren(qint64 deadline, bool& forcedResult) override {
    ++waitCalls; CHECK(deadline == 150100); forcedResult = forced; return exited ? Progress::Complete : Progress::Failed;
  }
  Progress launchB(qint64 deadline) override { ++launchCalls; CHECK(deadline == 150100);
    if (cancelDuringLaunch) CHECK(target->cancel() == Broker::Cancellation::Pending); return launched ? Progress::Complete : Progress::Failed; }
  Progress transferToB(const QByteArray& data, qint64 deadline) override {
    ++transferCalls; CHECK(deadline == 150100); frame = data;
    if (holdTransfer) return Progress::Pending;
    if (cancelDuringTransfer) CHECK(target->cancel() == Broker::Cancellation::Pending); return transferred ? Progress::Complete : Progress::Failed;
  }
  Progress cancelBAndAwaitAck(qint64 deadline) override { CHECK(deadline == 180100); if (holdAck) return Progress::Pending; if (lateAck) clock = deadline; return ack ? Progress::Complete : Progress::Failed; }
  Progress cleanupOwned(qint64 deadline) override { CHECK(deadline == 180100); return Progress::Complete; }
};

static void drive(Broker& broker) { for (int i = 0; i != 6; ++i) broker.step(); }

static Evidence sample()
{
  Evidence e;
  e.receipt = R"({"attempt":"42","core":"4","entry":"5","host":"1","revision":"3"})";
  e.observation = R"({"id":"7","paused":true,"positionMs":"5000","receivedNs":"200","requestedNs":"100"})";
  PlaybackCheckpoint checkpoint;
  checkpoint.intent.epoch = 1; checkpoint.intent.request = 2; checkpoint.intent.revision = 3;
  checkpoint.intent.desired = PlaybackIntent::Desired::Paused; checkpoint.intent.timelineQualified = true;
  checkpoint.positionKnown = true; checkpoint.positionMs = 5000;
  checkpoint.item = QStringLiteral("fixture-clip"); checkpoint.source = QStringLiteral("fixture-source");
  CHECK(PlaybackCheckpointCodec::encode(checkpoint, e.checkpoint));
  e.sourceAttempt = 42;
  e.grant = { 1, 2, 3, QByteArray(64, 'a'), 5000, true };
  return e;
}

static void framing()
{
  const Grant expected = sample().grant;
  const QByteArray frame = encodeGrant(expected);
  CHECK(!frame.isEmpty() && frame.size() <= 9216);
  Grant decoded;
  CHECK(decodeGrant(frame, expected, decoded) && decoded.grant == expected.grant);
  auto trustedBinding = expected; trustedBinding.positionMs = 0; trustedBinding.desiredPaused = false;
  CHECK(decodeGrant(frame, trustedBinding, decoded) && decoded.positionMs == 5000 && decoded.desiredPaused);
  CHECK(!decodeGrant(frame.left(frame.size() - 1), expected, decoded));
  CHECK(!decodeGrant(frame + 'x', expected, decoded));
  auto wrong = expected; wrong.run++;
  CHECK(!decodeGrant(frame, wrong, decoded));
  wrong = expected; wrong.assetHash[0] = 'b';
  CHECK(!decodeGrant(frame, wrong, decoded));
  wrong = expected; wrong.positionMs = 10000;
  CHECK(encodeGrant(wrong).isEmpty());
  CHECK(!decodeGrant(QByteArray(9217, 'x'), expected, decoded));
  QByteArray unknown = frame; unknown[unknown.size() - 2] = 'x';
  CHECK(!decodeGrant(unknown, expected, decoded));
  QByteArray extra = frame.mid(4);
  extra.insert(extra.size() - 1, ",\"extra\":true");
  QByteArray extraFrame(4, '\0');
  qToBigEndian<quint32>(quint32(extra.size()), reinterpret_cast<uchar*>(extraFrame.data()));
  CHECK(!decodeGrant(extraFrame + extra, expected, decoded));
  ChildGate child(&expected, 100);
  CHECK(child.accept(&expected, frame, expected, 10, true));
  CHECK(!child.accept(&expected, frame, expected, 10, true));
  CHECK(child.allowed(expected.revision));
  ChildGate timeout(&expected, 100);
  CHECK(!timeout.accept(&expected, frame, expected, 100, true));
  ChildGate partial(&expected, 100);
  CHECK(!partial.accept(&expected, frame, expected, 10, false));
  ChildGate other(&expected, 100);
  CHECK(!other.accept(&decoded, frame, expected, 10, true));
  CHECK(!other.accept(&expected, frame, expected, 10, true));
}

static void lifecycle()
{
  Fake ops;
  Broker broker(ops, ops.channel, sample().grant);
  Evidence evidence = sample();
  evidence.grant.grant = 0; // A does not choose the parent grant identifier.
  CHECK(!broker.capture(nullptr, evidence));
  CHECK(broker.capture(ops.channel, evidence));
  CHECK(!broker.capture(ops.channel, evidence));
  CHECK(broker.startReplacement());
  drive(broker);
  CHECK(broker.completed());
  CHECK(broker.burned() && ops.stopCalls == 1 && ops.waitCalls == 1 && ops.launchCalls == 1 && ops.transferCalls == 1);
  CHECK(broker.frozenEvidence().receipt == evidence.receipt);
  CHECK(broker.frozenEvidence().observation == evidence.observation);
  CHECK(broker.frozenEvidence().grant.grant == sample().grant.grant);
  CHECK(!broker.startReplacement());
  CHECK(broker.cancel() == Broker::Cancellation::Pending);
  broker.step();
  CHECK(broker.cancellation() == Broker::Cancellation::Acknowledged);
  CHECK(broker.cancel() == Broker::Cancellation::Acknowledged);
}

static void evidenceValidation()
{
  Evidence evidence = sample();
  CHECK(validateFixtureEvidence(evidence));
  auto wrong = evidence;
  wrong.sourceAttempt++;
  CHECK(!validateFixtureEvidence(wrong));
  wrong = evidence; wrong.grant.positionMs++;
  CHECK(!validateFixtureEvidence(wrong));
  wrong = evidence; wrong.grant.desiredPaused = false;
  CHECK(!validateFixtureEvidence(wrong));
  wrong = evidence; wrong.receipt.replace("\"core\":\"4\"", "\"core\":\"0\"");
  CHECK(!validateFixtureEvidence(wrong));
  wrong = evidence; wrong.observation.replace("\"receivedNs\":\"200\"", "\"receivedNs\":\"99\"");
  CHECK(!validateFixtureEvidence(wrong));
  wrong = evidence; wrong.checkpoint.append('x');
  CHECK(!validateFixtureEvidence(wrong));
  wrong = evidence;
  PlaybackCheckpoint forged;
  CHECK(PlaybackCheckpointCodec::decode(wrong.checkpoint, forged));
  forged.item = QStringLiteral("different-item");
  CHECK(PlaybackCheckpointCodec::encode(forged, wrong.checkpoint));
  Fake ops;
  Broker broker(ops, ops.channel, sample().grant);
  CHECK(!broker.capture(ops.channel, wrong));
}

static void failures()
{
  for (int which = 0; which != 6; ++which) {
    Fake ops;
    Broker broker(ops, ops.channel, sample().grant);
    CHECK(broker.capture(ops.channel, sample()));
    if (which == 0) ops.stopped = false;
    if (which == 1) ops.exited = false;
    if (which == 2) ops.forced = true;
    if (which == 3) ops.launched = false;
    if (which == 4) ops.transferred = false;
    if (which == 5) ops.clock = 150100;
    CHECK(broker.startReplacement() == (which != 5));
    drive(broker);
    CHECK(!broker.completed());
    CHECK(ops.launchCalls == (which == 3 || which == 4 ? 1 : 0));
    if (which == 3 || which == 4) CHECK(broker.burned());
    if (which != 5) CHECK(!broker.startReplacement());
  }
}

static bool checkedStop(void* p) { ++*static_cast<int*>(p); return true; }
static bool failedStop(void* p) { ++*static_cast<int*>(p); return false; }

static void ordering()
{
  const Grant grant = sample().grant;
  ChildGate child(&grant, 100);
  CHECK(child.accept(&grant, encodeGrant(grant), grant, 10, true));
  int stops = 0;
  CHECK(child.allowed(grant.revision)); // load boundary before cancellation
  CHECK(child.cancel(checkedStop, &stops)); // ACK follows local cancellation and checked Stop
  CHECK(stops == 1 && !child.allowed(grant.revision)); // later seek/unpause blocked
  CHECK(!child.cancel(checkedStop, &stops));
  ChildGate failed(&grant, 100);
  CHECK(failed.accept(&grant, encodeGrant(grant), grant, 10, true));
  CHECK(!failed.cancel(failedStop, &stops));
  CHECK(!failed.allowed(grant.revision));
  ChildGate before(&grant, 100);
  CHECK(before.cancel(checkedStop, &stops));
  CHECK(!before.accept(&grant, encodeGrant(grant), grant, 10, true));
  CHECK(!before.allowed(grant.revision));
  Fake ops;
  Broker broker(ops, ops.channel, sample().grant);
  CHECK(broker.capture(ops.channel, sample()));
  CHECK(broker.cancel() == Broker::Cancellation::Local);
  CHECK(!broker.startReplacement() && ops.launchCalls == 0);
  Fake regressed;
  Broker staleClock(regressed, regressed.channel, sample().grant);
  regressed.clock = 99;
  CHECK(!staleClock.capture(regressed.channel, sample()));
  for (int phase = 0; phase < 3; ++phase) {
    Fake running;
    Broker active(running, running.channel, sample().grant);
    running.target = &active;
    running.cancelDuringLaunch = phase == 0;
    running.cancelDuringTransfer = phase != 0;
    running.lateAck = phase == 2;
    CHECK(active.capture(running.channel, sample()));
    CHECK(active.startReplacement());
    drive(active);
    CHECK(!active.completed());
    CHECK(active.cancellation() == (phase == 2 ? Broker::Cancellation::Unconfirmed
                                               : Broker::Cancellation::Acknowledged));
    CHECK(active.burned());
  }
  Fake pending;
  Broker interleaved(pending, pending.channel, sample().grant);
  pending.holdTransfer = true;
  CHECK(interleaved.capture(pending.channel, sample()));
  CHECK(interleaved.startReplacement());
  drive(interleaved);
  CHECK(!interleaved.completed() && pending.transferCalls > 0);
  pending.holdAck = true;
  CHECK(interleaved.cancel() == Broker::Cancellation::Pending);
  interleaved.step();
  CHECK(interleaved.cancellation() == Broker::Cancellation::Pending);
  pending.holdAck = false;
  interleaved.step();
  CHECK(interleaved.cancellation() == Broker::Cancellation::Acknowledged);
}

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  framing(); lifecycle(); evidenceValidation(); failures(); ordering();
  return 0;
}
