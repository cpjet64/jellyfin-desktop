#include "PlaybackCheckpoint.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <cmath>

namespace {
using Intent = PlaybackIntent;
const auto limit = Intent::MaxSafeInteger;

bool logicalId(const QString& value)
{
  if (value.size() > 128) return false;
  for (const auto character : value) {
    const ushort c = character.unicode();
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
  }
  return true;
}

bool safeNumber(const QJsonValue& value, std::uint64_t& result)
{
  if (!value.isDouble()) return false;
  const double number = value.toDouble();
  if (!std::isfinite(number) || number < 0 || number > static_cast<double>(limit) || std::floor(number) != number)
    return false;
  result = static_cast<std::uint64_t>(number);
  return true;
}

QString eligibility(const PlaybackCheckpoint& value)
{
  if (!value.intent.epoch || !value.intent.request || value.server.isEmpty() || value.account.isEmpty() ||
      value.item.isEmpty() || value.source.isEmpty()) return QStringLiteral("missing_identity");
  if (!value.positionKnown || !value.intent.timelineQualified) return QStringLiteral("unknown_position");
  if (!value.tracksKnown || value.trackSource != value.source || value.audioTrack.isEmpty() || value.subtitleTrack.isEmpty())
    return QStringLiteral("unknown_tracks");
  if (!value.queueIdentityKnown || value.queueIdentity.isEmpty()) return QStringLiteral("unknown_queue");
  return QStringLiteral("producer_unqualified");
}

bool valid(const PlaybackCheckpoint& value)
{
  const auto& intent = value.intent;
  const int desired = static_cast<int>(intent.desired), reason = static_cast<int>(intent.reason);
  if (intent.epoch > limit || intent.request > limit || intent.revision > limit || desired < 0 || desired > 3 ||
      reason < 0 || reason > 8 || (intent.hasSeek && (intent.seekMs < 0 || std::uint64_t(intent.seekMs) > limit)) ||
      (value.positionKnown && (value.positionMs < 0 || std::uint64_t(value.positionMs) > limit || !intent.timelineQualified)))
    return false;
  return logicalId(value.server) && logicalId(value.account) && logicalId(value.item) && logicalId(value.source) &&
         logicalId(value.trackSource) && logicalId(value.audioTrack) && logicalId(value.subtitleTrack) &&
         logicalId(value.queueIdentity) && logicalId(value.historicalSession);
}

QJsonObject objectFor(const PlaybackCheckpoint& value)
{
  const auto& intent = value.intent;
  QJsonObject object;
  object.insert("version", 1);
  object.insert("epoch", double(intent.epoch)); object.insert("request", double(intent.request));
  object.insert("revision", double(intent.revision)); object.insert("desired", int(intent.desired));
  object.insert("intent_reason", int(intent.reason)); object.insert("timeline_qualified", intent.timelineQualified);
  object.insert("server", value.server); object.insert("account", value.account);
  object.insert("item", value.item); object.insert("source", value.source);
  object.insert("position_ms", value.positionKnown ? QJsonValue(double(value.positionMs)) : QJsonValue(QJsonValue::Null));
  object.insert("seek_ms", intent.hasSeek ? QJsonValue(double(intent.seekMs)) : QJsonValue(QJsonValue::Null));
  object.insert("tracks_known", value.tracksKnown); object.insert("track_source", value.trackSource);
  object.insert("audio_track", value.audioTrack); object.insert("subtitle_track", value.subtitleTrack);
  object.insert("queue_identity_known", value.queueIdentityKnown);
  object.insert("queue_identity", value.queueIdentity);
  object.insert("historical_session", value.historicalSession);
  object.insert("eligibility", eligibility(value)); object.insert("automatic_resume", false);
  return object;
}

bool numberField(const QJsonObject& object, const char* key, std::uint64_t& out)
{ return safeNumber(object.value(QLatin1String(key)), out); }

bool textField(const QJsonObject& object, const char* key, QString& out)
{
  const auto value = object.value(QLatin1String(key));
  if (!value.isString()) return false;
  out = value.toString();
  return logicalId(out);
}

bool nullableMs(const QJsonObject& object, const char* key, bool& present, std::int64_t& result)
{
  const auto value = object.value(QLatin1String(key));
  if (value.isNull()) { present = false; result = 0; return true; }
  std::uint64_t number = 0;
  if (!safeNumber(value, number)) return false;
  present = true; result = static_cast<std::int64_t>(number); return true;
}

bool parseObject(const QJsonObject& object, PlaybackCheckpoint& value)
{
  const QSet<QString> fields = {"version", "epoch", "request", "revision", "desired", "intent_reason",
    "timeline_qualified", "server", "account", "item", "source", "position_ms", "seek_ms", "tracks_known",
    "track_source", "audio_track", "subtitle_track", "queue_identity_known", "queue_identity", "historical_session",
    "eligibility", "automatic_resume"};
  const auto keys = object.keys();
  if (QSet<QString>(keys.begin(), keys.end()) != fields) return false;
  std::uint64_t version = 0, desired = 0, reason = 0;
  auto& intent = value.intent;
  if (!numberField(object, "version", version) || version != 1 || !numberField(object, "epoch", intent.epoch) ||
      !numberField(object, "request", intent.request) || !numberField(object, "revision", intent.revision) ||
      !numberField(object, "desired", desired) || desired > 3 || !numberField(object, "intent_reason", reason) || reason > 8)
    return false;
  intent.desired = static_cast<Intent::Desired>(desired); intent.reason = static_cast<Intent::Reason>(reason);
  for (const char* key : {"timeline_qualified", "tracks_known", "queue_identity_known", "automatic_resume"})
    if (!object.value(QLatin1String(key)).isBool()) return false;
  if (object.value("automatic_resume").toBool()) return false;
  intent.timelineQualified = object.value("timeline_qualified").toBool();
  value.tracksKnown = object.value("tracks_known").toBool();
  value.queueIdentityKnown = object.value("queue_identity_known").toBool();
  if (!textField(object, "server", value.server) || !textField(object, "account", value.account) ||
      !textField(object, "item", value.item) || !textField(object, "source", value.source) ||
      !textField(object, "track_source", value.trackSource) || !textField(object, "audio_track", value.audioTrack) ||
      !textField(object, "subtitle_track", value.subtitleTrack) || !textField(object, "queue_identity", value.queueIdentity) ||
      !textField(object, "historical_session", value.historicalSession) ||
      !nullableMs(object, "position_ms", value.positionKnown, value.positionMs) ||
      !nullableMs(object, "seek_ms", intent.hasSeek, intent.seekMs)) return false;
  return valid(value) && object.value("eligibility").isString() && object.value("eligibility").toString() == eligibility(value);
}
} // namespace

bool PlaybackCheckpointCodec::encode(const PlaybackCheckpoint& value, QByteArray& output)
{
  if (!valid(value)) return false;
  const auto bytes = QJsonDocument(objectFor(value)).toJson(QJsonDocument::Compact);
  if (bytes.size() > MaxBytes) return false;
  output = bytes;
  return true;
}

bool PlaybackCheckpointCodec::decode(const QByteArray& input, PlaybackCheckpoint& output)
{
  if (input.size() > MaxBytes) return false;
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(input, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
  PlaybackCheckpoint parsed;
  if (!parseObject(document.object(), parsed)) return false;
  QByteArray canonical;
  if (!encode(parsed, canonical) || canonical != input) return false;
  output = parsed;
  return true;
}
