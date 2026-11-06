#include "RestartBroker.h"
#include "player/PlaybackCheckpoint.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <limits>

namespace FixtureRestart {
namespace {
bool canonicalObject(const QByteArray& data, int count, QJsonObject& output)
{
  if (data.isEmpty() || data.size() > 8192) return false;
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(data, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject() ||
      document.object().size() != count || document.toJson(QJsonDocument::Compact) != data) return false;
  output = document.object();
  return true;
}

bool number(const QJsonObject& object, const char* field, quint64& value,
            quint64 maximum = PlaybackIntent::MaxSafeInteger)
{
  const auto element = object.value(QLatin1String(field));
  if (!element.isString()) return false;
  bool ok = false;
  value = element.toString().toULongLong(&ok);
  return ok && element.toString() == QString::number(value) && value <= maximum;
}
}

bool validateFixtureEvidence(const Evidence& evidence)
{
  QJsonObject receipt, observation;
  if (!canonicalObject(evidence.receipt, 5, receipt) || !canonicalObject(evidence.observation, 5, observation)) return false;
  quint64 host, core, revision, attempt, entry, id, requested, received, position;
  if (!number(receipt, "host", host) || !number(receipt, "core", core) || !number(receipt, "revision", revision) ||
      !number(receipt, "attempt", attempt) || !number(receipt, "entry", entry) ||
      !number(observation, "id", id) || !number(observation, "requestedNs", requested, std::numeric_limits<qint64>::max()) ||
      !number(observation, "receivedNs", received, std::numeric_limits<qint64>::max()) || !number(observation, "positionMs", position) ||
      !observation.value("paused").isBool() || !host || !core || !revision || !attempt || !entry || !id ||
      requested > received || position >= 10000 || attempt != evidence.sourceAttempt ||
      qint64(position) != evidence.grant.positionMs || observation.value("paused").toBool() != evidence.grant.desiredPaused)
    return false;
  PlaybackCheckpoint checkpoint;
  QByteArray canonical;
  if (!PlaybackCheckpointCodec::decode(evidence.checkpoint, checkpoint) ||
      !PlaybackCheckpointCodec::encode(checkpoint, canonical) || canonical != evidence.checkpoint ||
      checkpoint.intent.revision != revision || !checkpoint.positionKnown || !checkpoint.intent.timelineQualified ||
      checkpoint.positionMs != qint64(position) || checkpoint.item != QLatin1String("fixture-clip") ||
      checkpoint.source != QLatin1String("fixture-source")) return false;
  return checkpoint.intent.desired == (evidence.grant.desiredPaused ? PlaybackIntent::Desired::Paused : PlaybackIntent::Desired::Playing);
}
}
