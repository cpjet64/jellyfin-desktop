#include "fixture_restart/RestartWire.h"
#include "fixture_restart/RestartSource.h"
#include "player/PlaybackCheckpoint.h"
#include <QCoreApplication>
#include <QtEndian>
#include <cstdlib>
#define CHECK(value) do { if (!(value)) std::abort(); } while (false)

using namespace FixtureRestart;

static Evidence sample()
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
  e.sourceAttempt = 42;
  e.grant = {1, 2, 3, QByteArray(64, 'a'), 3000, true};
  return e;
}

static void evidence()
{
  const Evidence input = sample();
  const QByteArray frame = encodeEvidence(input);
  CHECK(!frame.isEmpty() && frame.size() <= 9216);
  Evidence decoded;
  Grant binding = input.grant; binding.positionMs = 0; binding.desiredPaused = false;
  CHECK(decodeEvidence(frame, binding, true, decoded));
  CHECK(decoded.receipt == input.receipt && decoded.observation == input.observation && decoded.checkpoint == input.checkpoint);
  CHECK(decoded.grant.positionMs == 3000 && decoded.grant.desiredPaused);
  CHECK(!decodeEvidence(frame, binding, false, decoded));
  CHECK(!decodeEvidence(frame.left(frame.size() - 1), binding, true, decoded));
  CHECK(!decodeEvidence(frame + 'x', binding, true, decoded));
  CHECK(!decodeEvidence(QByteArray(9217, 'x'), binding, true, decoded));
  QByteArray data = frame.mid(4); data.insert(data.size() - 1, ",\"extra\":true");
  QByteArray extra(4, '\0');
  qToBigEndian<quint32>(quint32(data.size()), reinterpret_cast<uchar*>(extra.data()));
  CHECK(!decodeEvidence(extra + data, binding, true, decoded));
  Evidence bad = input; bad.sourceAttempt++;
  CHECK(encodeEvidence(bad).isEmpty());
  bad = input;
  bad.grant.positionMs = 2800;
  bad.observation.replace("\"positionMs\":\"3000\"", "\"positionMs\":\"2800\"");
  PlaybackCheckpoint old;
  CHECK(PlaybackCheckpointCodec::decode(bad.checkpoint, old));
  old.positionMs = 2800;
  CHECK(PlaybackCheckpointCodec::encode(old, bad.checkpoint));
  CHECK(validateFixtureEvidence(bad) && encodeEvidence(bad).isEmpty());
}

static void controls()
{
  for (int kind = 1; kind <= 4; ++kind) {
    Control input{ControlKind(kind), 123, 456}, decoded;
    const QByteArray bytes = encodeControl(input);
    CHECK(bytes.size() == 24 && decodeControl(bytes, decoded));
    CHECK(decoded.kind == input.kind && decoded.sequence == input.sequence && decoded.revision == input.revision);
    CHECK(!decodeControl(bytes.left(23), decoded));
    CHECK(!decodeControl(bytes + 'x', decoded));
    QByteArray reserved = bytes; reserved[5] = 1;
    CHECK(!decodeControl(reserved, decoded));
  }
  CHECK(encodeControl({ControlKind::Ack, 0, 1}).isEmpty());
  CHECK(encodeControl({ControlKind(5), 1, 1}).isEmpty());
}

static void sourceOrdering()
{
  int evidenceChannel, controlChannel, captures = 0, stops = 0;
  const Evidence frozen = sample();
  RestartSource source(&evidenceChannel, &controlChannel, frozen.grant.revision,
    [&](Evidence& out) { ++captures; out = frozen; return true; },
    [&](const Evidence& value) { ++stops; return value.receipt == frozen.receipt; });
  QByteArray frame, ack;
  CHECK(!source.freeze(&controlChannel, frame));
  CHECK(source.freeze(&evidenceChannel, frame));
  CHECK(!source.freeze(&evidenceChannel, frame) && captures == 1);
  CHECK(!source.control(&evidenceChannel, encodeControl({ControlKind::InternalStop, 1, 3}), ack));
  CHECK(source.control(&controlChannel, encodeControl({ControlKind::InternalStop, 1, 3}), ack));
  CHECK(!source.control(&controlChannel, encodeControl({ControlKind::InternalStop, 1, 3}), ack));
  CHECK(stops == 1);
  Control parsed;
  CHECK(decodeControl(ack, parsed) && parsed.kind == ControlKind::Ack && parsed.revision == 3);
  RestartSource* owner = nullptr;
  RestartSource reentrant(&evidenceChannel, &controlChannel, frozen.grant.revision,
    [&](Evidence& out) { QByteArray extra; CHECK(!owner->freeze(&evidenceChannel, extra)); out = frozen; return true; },
    [](const Evidence&) { return true; });
  owner = &reentrant;
  CHECK(reentrant.freeze(&evidenceChannel, frame));
}

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  evidence(); controls(); sourceOrdering();
  return 0;
}
