#include "RestartSource.h"
#include "player/PlaybackCheckpoint.h"
#include <QCoreApplication>
#include <cstdlib>
#include <stdexcept>
#define CHECK(value) do { if (!(value)) std::abort(); } while (false)
using namespace FixtureRestart;
using Status = HealthyRestart::AsyncStop::Status;

namespace {
Evidence sample()
{
  Evidence e;
  e.receipt = R"({"attempt":"42","core":"4","entry":"5","host":"1","revision":"3"})";
  e.observation = R"({"id":"7","paused":true,"positionMs":"3000","receivedNs":"200","requestedNs":"100"})";
  PlaybackCheckpoint checkpoint;
  checkpoint.intent.epoch = 1; checkpoint.intent.request = 2; checkpoint.intent.revision = 3;
  checkpoint.intent.desired = PlaybackIntent::Desired::Paused; checkpoint.intent.timelineQualified = true;
  checkpoint.positionKnown = true; checkpoint.positionMs = 3000;
  checkpoint.item = QStringLiteral("fixture-clip"); checkpoint.source = QStringLiteral("fixture-source");
  CHECK(PlaybackCheckpointCodec::encode(checkpoint, e.checkpoint));
  e.sourceAttempt = 42; e.grant = {1, 2, 3, QByteArray(64, 'a'), 3000, true};
  return e;
}
QByteArray control() { return encodeControl({ControlKind::InternalStop, 1, 3}); }
void failedBeginNeverAcknowledges(bool throws)
{
  int evidenceChannel, controlChannel, polls = 0; const auto frozen = sample();
  RestartSource source(&evidenceChannel, &controlChannel, 3,
    [&](Evidence& out) { out = frozen; return true; },
    [&](const Evidence&, qint64) { if (throws) throw std::runtime_error("test"); return false; },
    [&] { ++polls; return Status::Complete; });
  QByteArray frame, response("stale");
  CHECK(source.freeze(&evidenceChannel, frame));
  CHECK(!source.beginControl(&controlChannel, control(), 10));
  CHECK(source.pollControl(&controlChannel, response) == Status::Failed && response.isEmpty() && polls == 0);
  CHECK(!source.beginControl(&controlChannel, control(), 10));
}
void pendingAndReentrantPoll()
{
  int evidenceChannel, controlChannel, polls = 0; const auto frozen = sample();
  RestartSource* owner = nullptr; bool reentrantRejected = false;
  RestartSource source(&evidenceChannel, &controlChannel, 3,
    [&](Evidence& out) { out = frozen; return true; },
    [&](const Evidence&, qint64) {
      QByteArray nested("stale");
      reentrantRejected = owner->pollControl(&controlChannel, nested) == Status::Failed && nested.isEmpty();
      return true;
    },
    [&] {
      ++polls;
      if (polls == 1) return Status::Pending;
      QByteArray nested("stale");
      reentrantRejected = reentrantRejected && owner->pollControl(&controlChannel, nested) == Status::Failed && nested.isEmpty();
      return Status::Complete;
    });
  owner = &source;
  QByteArray frame, response("stale");
  CHECK(source.freeze(&evidenceChannel, frame));
  CHECK(source.beginControl(&controlChannel, control(), 10));
  CHECK(source.pollControl(&evidenceChannel, response) == Status::Failed && response.isEmpty());
  CHECK(source.pollControl(&controlChannel, response) == Status::Pending && response.isEmpty());
  CHECK(source.pollControl(&controlChannel, response) == Status::Complete && reentrantRejected);
  Control ack; CHECK(decodeControl(response, ack) && ack.kind == ControlKind::Ack);
  CHECK(source.pollControl(&controlChannel, response) == Status::Failed && response.isEmpty());
}
}
int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  failedBeginNeverAcknowledges(false); failedBeginNeverAcknowledges(true); pendingAndReentrantPoll();
}
