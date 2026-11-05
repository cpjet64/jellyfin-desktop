#include "player/PlaybackCheckpoint.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <cstdlib>
#include <limits>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "check failed at line %d\n", __LINE__); std::abort(); } } while (false)

static PlaybackCheckpoint sample()
{
  PlaybackCheckpoint value;
  value.intent.epoch = 7; value.intent.request = 3; value.intent.revision = 9;
  value.intent.desired = PlaybackIntent::Desired::Paused;
  value.intent.timelineQualified = true;
  value.intent.hasSeek = true; value.intent.seekMs = 4000;
  value.server = "server-1"; value.account = "account-2"; value.item = "item-3"; value.source = "source-4";
  value.positionKnown = true; value.positionMs = 12000;
  value.tracksKnown = true; value.trackSource = "source-4"; value.audioTrack = "audio-1"; value.subtitleTrack = "none";
  value.queueIdentityKnown = true; value.queueIdentity = "queue-1"; value.historicalSession = "old-session";
  return value;
}

static QByteArray encoded(const PlaybackCheckpoint& value)
{
  QByteArray bytes;
  CHECK(PlaybackCheckpointCodec::encode(value, bytes));
  return bytes;
}

static QByteArray changed(const QByteArray& original, const QString& field, const QJsonValue& value)
{
  auto object = QJsonDocument::fromJson(original).object();
  object.insert(field, value);
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

static void roundTripAndUnknowns()
{
  const auto value = sample();
  const auto bytes = encoded(value);
  const auto object = QJsonDocument::fromJson(bytes).object();
  CHECK(object.value("automatic_resume").toBool() == false);
  CHECK(object.value("eligibility").toString() == "producer_unqualified");
  CHECK(object.value("position_ms").toInt() == 12000 && object.value("seek_ms").toInt() == 4000);
  PlaybackCheckpoint restored;
  CHECK(PlaybackCheckpointCodec::decode(bytes, restored));
  CHECK(restored.intent.epoch == 7 && restored.intent.request == 3 && restored.intent.revision == 9);
  CHECK(restored.positionKnown && restored.positionMs == 12000 && restored.intent.seekMs == 4000);
  CHECK(restored.trackSource == restored.source && restored.historicalSession == "old-session");
  auto unknown = value;
  unknown.positionKnown = false; unknown.intent.timelineQualified = false;
  CHECK(QJsonDocument::fromJson(encoded(unknown)).object().value("eligibility").toString() == "unknown_position");
  unknown = value; unknown.tracksKnown = false;
  CHECK(QJsonDocument::fromJson(encoded(unknown)).object().value("eligibility").toString() == "unknown_tracks");
  unknown = value; unknown.queueIdentityKnown = false;
  CHECK(QJsonDocument::fromJson(encoded(unknown)).object().value("eligibility").toString() == "unknown_queue");
  unknown = value; unknown.source.clear();
  CHECK(QJsonDocument::fromJson(encoded(unknown)).object().value("eligibility").toString() == "missing_identity");
}

static void invalidRecordsFailWithoutOutput()
{
  const auto bytes = encoded(sample());
  const QByteArray invalid[] = {
    changed(bytes, "url", "https://example.invalid/token"), changed(bytes, "authorization", "secret"),
    changed(bytes, "server", "https://example.invalid"), changed(bytes, "revision", -1),
    changed(bytes, "request", 1.5), changed(bytes, "epoch", 9007199254740992.0),
    changed(bytes, "position_ms", -1), changed(bytes, "automatic_resume", true),
    changed(bytes, "eligibility", "eligible"), QByteArray(PlaybackCheckpointCodec::MaxBytes, 'x'),
    QByteArray(PlaybackCheckpointCodec::MaxBytes + 1, 'x'), QByteArray("{\"epoch\":7,") + bytes.mid(1),
    QByteArray("{\"revision\":NaN}"), QByteArray("{\"revision\":1e999}")};
  for (const auto& input : invalid) {
    auto result = sample();
    result.item = "unchanged";
    CHECK(!PlaybackCheckpointCodec::decode(input, result));
    CHECK(result.item == "unchanged");
  }
  auto invalidValue = sample();
  invalidValue.server = "https://secret.invalid";
  QByteArray output("unchanged");
  CHECK(!PlaybackCheckpointCodec::encode(invalidValue, output) && output == "unchanged");
  invalidValue = sample(); invalidValue.positionMs = -1;
  CHECK(!PlaybackCheckpointCodec::encode(invalidValue, output) && output == "unchanged");
}

static void intentIsNeverGuessed()
{
  auto value = sample();
  value.intent.timelineQualified = false;
  QByteArray output("unchanged");
  CHECK(!PlaybackCheckpointCodec::encode(value, output));
  value.positionKnown = false;
  const auto object = QJsonDocument::fromJson(encoded(value)).object();
  CHECK(object.value("position_ms").isNull());
  CHECK(object.value("seek_ms").toInt() == 4000);
  CHECK(object.value("automatic_resume").toBool() == false);
}

int main(int argc, char** argv)
{
  QCoreApplication application(argc, argv);
  roundTripAndUnknowns();
  invalidRecordsFailWithoutOutput();
  intentIsNeverGuessed();
}
