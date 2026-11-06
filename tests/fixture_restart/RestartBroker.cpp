#include "RestartBroker.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <limits>

namespace FixtureRestart {
namespace {
bool valid(const Grant& g)
{
  if (!g.run || !g.grant || !g.revision || g.positionMs < 0 || g.positionMs >= 10000 || g.assetHash.size() != 64)
    return false;
  for (const char c : g.assetHash) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}

QByteArray payload(const Grant& g)
{
  QJsonObject object;
  object.insert("asset", QString::fromLatin1(g.assetHash));
  object.insert("fixture", "TestClip");
  object.insert("grant", QString::number(g.grant));
  object.insert("paused", g.desiredPaused);
  object.insert("positionMs", QString::number(g.positionMs));
  object.insert("revision", QString::number(g.revision));
  object.insert("run", QString::number(g.run));
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
}

QByteArray encodeGrant(const Grant& grant)
{
  if (!valid(grant)) return {};
  const QByteArray data = payload(grant);
  if (data.size() + 4 > 9216) return {};
  QByteArray frame(4, '\0');
  qToBigEndian<quint32>(quint32(data.size()), reinterpret_cast<uchar*>(frame.data()));
  return frame + data;
}

bool decodeGrant(const QByteArray& frame, const Grant& expected, Grant& output)
{
  if (!expected.run || !expected.grant || !expected.revision || frame.size() < 5 || frame.size() > 9216) return false;
  const quint32 size = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(frame.constData()));
  if (size != quint32(frame.size() - 4)) return false;
  const QByteArray data = frame.mid(4);
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(data, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
  const QJsonObject object = document.object();
  if (object.size() != 7 || !object.value("asset").isString() || object.value("fixture").toString() != "TestClip" ||
      !object.value("run").isString() || !object.value("grant").isString() || !object.value("revision").isString() ||
      !object.value("positionMs").isString() || !object.value("paused").isBool()) return false;
  bool runOk = false, grantOk = false, revisionOk = false, positionOk = false;
  Grant parsed;
  parsed.run = object.value("run").toString().toULongLong(&runOk);
  parsed.grant = object.value("grant").toString().toULongLong(&grantOk);
  parsed.revision = object.value("revision").toString().toULongLong(&revisionOk);
  parsed.positionMs = object.value("positionMs").toString().toLongLong(&positionOk);
  parsed.desiredPaused = object.value("paused").toBool();
  parsed.assetHash = object.value("asset").toString().toLatin1();
  if (!runOk || !grantOk || !revisionOk || !positionOk || !valid(parsed) || data != payload(parsed) ||
      parsed.run != expected.run || parsed.grant != expected.grant || parsed.revision != expected.revision ||
      parsed.assetHash != expected.assetHash) return false;
  output = parsed;
  return true;
}

Broker::Broker(Operations& operations, const void* heldChannel, const Grant& parentBinding)
  : operations_(operations), channel_(heldChannel), parentBinding_(parentBinding), start_(-1), normal_(0), overall_(0)
{
  const qint64 start = operations_.nowMs();
  if (start >= 0 && start <= std::numeric_limits<qint64>::max() - 180000) {
    start_ = start;
    normal_ = start + 150000;
    overall_ = start + 180000;
  }
}

bool Broker::withinNormal() const
{
  const qint64 now = operations_.nowMs();
  return start_ >= 0 && now >= start_ && now < normal_;
}

bool Broker::capture(const void* channel, const Evidence& evidence)
{
  Grant derived = parentBinding_;
  derived.positionMs = evidence.grant.positionMs;
  derived.desiredPaused = evidence.grant.desiredPaused;
  if (captured_ || canceled_ || burned_ || !withinNormal() || !channel_ || channel != channel_ ||
      !operations_.ownsAChannel(channel_) || !valid(parentBinding_) ||
      evidence.grant.run != parentBinding_.run || evidence.grant.revision != parentBinding_.revision ||
      evidence.grant.assetHash != parentBinding_.assetHash ||
      !evidence.sourceAttempt || evidence.receipt.isEmpty() || evidence.observation.isEmpty() ||
      evidence.checkpoint.isEmpty() || evidence.receipt.size() > 8192 || evidence.observation.size() > 8192 ||
      evidence.checkpoint.size() > 8192 || !valid(derived) || !validateFixtureEvidence(evidence) ||
      !operations_.validateEvidence(evidence)) return false;
  if (!withinNormal() || canceled_ || !operations_.ownsAChannel(channel_)) return false;
  evidence_ = evidence;
  evidence_.grant = derived;
  captured_ = true;
  return true;
}

bool Broker::startReplacement()
{
  if (!captured_ || canceled_ || burned_ || replacementStarted_ || !withinNormal() || !operations_.ownsAChannel(channel_)) return false;
  replacementStarted_ = true;
  phase_ = Phase::Stop;
  return true;
}

void Broker::step()
{
  if (canceled_ && cancellation_ == Cancellation::Pending) { stepCancellation(); return; }
  if (canceled_ && phase_ != Phase::Cleanup && phase_ != Phase::Failed && phase_ != Phase::Unconfirmed)
    phase_ = Phase::Cleanup;
  if (phase_ == Phase::Idle || phase_ == Phase::Done || phase_ == Phase::Failed || phase_ == Phase::Unconfirmed) return;
  using Progress = Operations::Progress;
  if (phase_ == Phase::Cleanup) {
    const qint64 before = operations_.nowMs();
    if (before < start_ || before >= overall_) { phase_ = Phase::Unconfirmed; return; }
    const Progress cleanup = operations_.cleanupOwned(overall_);
    const qint64 after = operations_.nowMs();
    if (after < before || after >= overall_ || cleanup == Progress::Failed) phase_ = Phase::Unconfirmed;
    else if (cleanup == Progress::Complete) phase_ = Phase::Failed;
    return;
  }
  if (!withinNormal()) { phase_ = Phase::Cleanup; return; }
  Progress result = Progress::Failed;
  bool forced = true;
  switch (phase_) {
  case Phase::Stop: result = operations_.internalReplacementStop(normal_); break;
  case Phase::Wait: result = operations_.waitAAndChildren(normal_, forced); break;
  case Phase::Launch: result = operations_.launchB(normal_); break;
  case Phase::Transfer: result = operations_.transferToB(frame_, normal_); break;
  default: return;
  }
  if (canceled_) { if (cancellation_ == Cancellation::Pending) stepCancellation(); return; }
  if (!withinNormal() || result == Progress::Failed || (phase_ == Phase::Wait && result == Progress::Complete && forced)) {
    phase_ = Phase::Cleanup; return;
  }
  if (result == Progress::Pending) return;
  if (phase_ == Phase::Stop) phase_ = Phase::Wait;
  else if (phase_ == Phase::Wait) {
    frame_ = encodeGrant(evidence_.grant);
    if (frame_.isEmpty()) { phase_ = Phase::Cleanup; return; }
    burned_ = true;
    bLaunchStarted_ = true;
    phase_ = Phase::Launch;
  } else if (phase_ == Phase::Launch) phase_ = Phase::Transfer;
  else phase_ = Phase::Done;
}

Broker::Cancellation Broker::cancel()
{
  if (canceled_) return cancellation_;
  canceled_ = true;
  burned_ = true;
  cancellation_ = bLaunchStarted_ ? Cancellation::Pending : Cancellation::Local;
  return cancellation_;
}

void Broker::stepCancellation()
{
  if (cancellation_ != Cancellation::Pending) return;
  const qint64 now = operations_.nowMs();
  if (now < start_ || now >= overall_) { cancellation_ = Cancellation::Unconfirmed; return; }
  const auto result = operations_.cancelBAndAwaitAck(overall_);
  const qint64 after = operations_.nowMs();
  if (after < now || after >= overall_ || result == Operations::Progress::Failed) cancellation_ = Cancellation::Unconfirmed;
  else if (result == Operations::Progress::Complete) cancellation_ = Cancellation::Acknowledged;
}

bool ChildGate::accept(const void* channel, const QByteArray& frame, const Grant& expected, qint64 now, bool eof)
{
  if (attempted_ || canceled_) return false;
  attempted_ = true;
  if (!channel_ || channel != channel_ || !eof || now < 0 || deadline_ <= now) return false;
  accepted_ = decodeGrant(frame, expected, grant_);
  return accepted_;
}

bool ChildGate::cancel(bool (*checkedStop)(void*), void* context)
{
  if (canceled_ || !checkedStop) return false;
  canceled_ = true;
  return checkedStop(context);
}

bool ChildGate::allowed(quint64 revision) const
{
  return accepted_ && !canceled_ && revision == grant_.revision;
}
}
