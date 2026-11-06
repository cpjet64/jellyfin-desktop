#include "RestartWire.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

namespace FixtureRestart {
namespace {
QByteArray base64(const QByteArray& value) { return value.toBase64(QByteArray::Base64Encoding); }
QByteArray payload(const Evidence& value)
{
  QJsonObject object{{"attempt", QString::number(value.sourceAttempt)},
    {"checkpoint", QString::fromLatin1(base64(value.checkpoint))},
    {"observation", QString::fromLatin1(base64(value.observation))},
    {"receipt", QString::fromLatin1(base64(value.receipt))}};
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
bool strictBase64(const QJsonValue& value, QByteArray& output)
{
  if (!value.isString()) return false;
  const QByteArray input = value.toString().toLatin1();
  const auto decoded = QByteArray::fromBase64Encoding(input, QByteArray::AbortOnBase64DecodingErrors);
  if (!decoded || base64(decoded.decoded) != input || decoded.decoded.isEmpty() || decoded.decoded.size() > 8192) return false;
  output = decoded.decoded;
  return true;
}
}

QByteArray encodeEvidence(const Evidence& evidence)
{
  if (evidence.grant.positionMs < 2900 || evidence.grant.positionMs > 3100 || !validateFixtureEvidence(evidence)) return {};
  const QByteArray data = payload(evidence);
  if (data.size() + 4 > 9216) return {};
  QByteArray frame(4, '\0');
  qToBigEndian<quint32>(quint32(data.size()), reinterpret_cast<uchar*>(frame.data()));
  return frame + data;
}

bool decodeEvidence(const QByteArray& frame, const Grant& parentBinding, bool eof, Evidence& result)
{
  if (!eof || frame.size() < 5 || frame.size() > 9216 ||
      qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(frame.constData())) != quint32(frame.size() - 4)) return false;
  const QByteArray data = frame.mid(4);
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(data, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject() || document.object().size() != 4) return false;
  const auto object = document.object();
  if (!object.value("attempt").isString()) return false;
  Evidence candidate;
  bool ok = false;
  candidate.sourceAttempt = object.value("attempt").toString().toULongLong(&ok);
  candidate.grant = parentBinding;
  if (!ok || !candidate.sourceAttempt || !strictBase64(object.value("checkpoint"), candidate.checkpoint) ||
      !strictBase64(object.value("observation"), candidate.observation) || !strictBase64(object.value("receipt"), candidate.receipt)) return false;
  QJsonParseError observationError;
  const auto observation = QJsonDocument::fromJson(candidate.observation, &observationError).object();
  bool positionOk = false;
  candidate.grant.positionMs = observation.value("positionMs").toString().toLongLong(&positionOk);
  if (observationError.error != QJsonParseError::NoError || !positionOk || !observation.value("paused").isBool()) return false;
  candidate.grant.desiredPaused = observation.value("paused").toBool();
  if (candidate.grant.positionMs < 2900 || candidate.grant.positionMs > 3100 ||
      data != payload(candidate) || !validateFixtureEvidence(candidate)) return false;
  result = candidate;
  return true;
}

QByteArray encodeControl(const Control& value)
{
  const auto kind = quint8(value.kind);
  if (kind < 1 || kind > 4 || !value.sequence || !value.revision) return {};
  QByteArray bytes(24, '\0');
  bytes[0] = 'J'; bytes[1] = 'R'; bytes[2] = 'C'; bytes[3] = '1';
  bytes[4] = char(kind);
  qToBigEndian<quint64>(value.sequence, reinterpret_cast<uchar*>(bytes.data() + 8));
  qToBigEndian<quint64>(value.revision, reinterpret_cast<uchar*>(bytes.data() + 16));
  return bytes;
}

bool decodeControl(const QByteArray& bytes, Control& result)
{
  if (bytes.size() != 24 || bytes.left(4) != "JRC1" || bytes[5] || bytes[6] || bytes[7]) return false;
  Control candidate{ControlKind(quint8(bytes[4])), qFromBigEndian<quint64>(reinterpret_cast<const uchar*>(bytes.constData() + 8)),
                    qFromBigEndian<quint64>(reinterpret_cast<const uchar*>(bytes.constData() + 16))};
  if (encodeControl(candidate) != bytes) return false;
  result = candidate;
  return true;
}
}
