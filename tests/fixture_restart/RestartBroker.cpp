#include "RestartBroker.h"
#include "RestartWire.h"

#include <QJsonDocument>
#include <QCryptographicHash>
#include <QScopedValueRollback>
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
  object.insert("version", 1);
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

static bool decodeGrantValue(const QByteArray& frame, Grant& output)
{
  if (frame.size() < 5 || frame.size() > 9216) return false;
  const quint32 size = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(frame.constData()));
  if (size != quint32(frame.size() - 4)) return false;
  const QByteArray data = frame.mid(4);
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(data, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
  const QJsonObject object = document.object();
  if (object.size() != 8 || object.value("version") != 1 || !object.value("asset").isString() || object.value("fixture").toString() != "TestClip" ||
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
  if (!runOk || !grantOk || !revisionOk || !positionOk || !valid(parsed) || data != payload(parsed)) return false;
  output = parsed;
  return true;
}

bool decodeGrant(const QByteArray& frame, const Grant& expected, Grant& output)
{
  Grant parsed;
  if (!valid(expected) || !decodeGrantValue(frame, parsed) || parsed.run != expected.run ||
      parsed.grant != expected.grant || parsed.revision != expected.revision || parsed.assetHash != expected.assetHash ||
      parsed.positionMs != expected.positionMs || parsed.desiredPaused != expected.desiredPaused) return false;
  output = parsed;
  return true;
}

bool decodeCommittedGrant(const QByteArray& frame, quint64 expectedRun, const QByteArray& expectedDigest, Grant& output)
{
  if (!expectedRun || expectedDigest.size() != 32 || frame.size() > 9216 ||
      QCryptographicHash::hash(frame, QCryptographicHash::Sha256) != expectedDigest) return false;
  Grant parsed;
  if (!decodeGrantValue(frame, parsed) || parsed.run != expectedRun) return false;
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
  if (dispatchActive_) return false;
  QScopedValueRollback<bool> guard(dispatchActive_, true);
  const Evidence candidate = evidence;
  Grant derived = parentBinding_;
  derived.positionMs = candidate.grant.positionMs;
  derived.desiredPaused = candidate.grant.desiredPaused;
  if (captured_ || canceled_ || burned_ || !withinNormal() || !channel_ || channel != channel_ ||
      !operations_.ownsAChannel(channel_) || !valid(parentBinding_) ||
      candidate.grant.run != parentBinding_.run || candidate.grant.revision != parentBinding_.revision ||
      candidate.grant.assetHash != parentBinding_.assetHash ||
      !candidate.sourceAttempt || candidate.receipt.isEmpty() || candidate.observation.isEmpty() ||
      candidate.checkpoint.isEmpty() || candidate.receipt.size() > 8192 || candidate.observation.size() > 8192 ||
      candidate.checkpoint.size() > 8192 || !valid(derived) || !validateFixtureEvidence(candidate) ||
      !operations_.validateEvidence(candidate)) return false;
  if (!withinNormal() || canceled_ || !operations_.ownsAChannel(channel_)) return false;
  if (canceled_ || !withinNormal()) return false;
  evidence_ = candidate;
  evidence_.grant = derived;
  captured_ = true;
  return true;
}

bool Broker::startReplacement()
{
  if (dispatchActive_) return false;
  QScopedValueRollback<bool> guard(dispatchActive_, true);
  if (!captured_ || canceled_ || burned_ || replacementStarted_ || !withinNormal() || !operations_.ownsAChannel(channel_)) return false;
  if (canceled_ || !withinNormal()) return false;
  replacementStarted_ = true;
  phase_ = Phase::Stop;
  return true;
}

void Broker::step()
{
  if (dispatchActive_) return;
  QScopedValueRollback<bool> guard(dispatchActive_, true);
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
  if (!withinNormal()) {
    if (bLaunchStarted_) { cancel(); stepCancellation(); }
    else phase_ = Phase::Cleanup;
    return;
  }
  if (phase_ == Phase::Launch || phase_ == Phase::Transfer || phase_ == Phase::Accepted ||
      phase_ == Phase::Permit || phase_ == Phase::Monitor) {
    const bool eligible = operations_.eligibleForB(evidence_.grant);
    if (!eligible || canceled_ || !withinNormal()) {
      if (!eligible && !canceled_) cancel();
      if (canceled_ && cancellation_ == Cancellation::Pending) stepCancellation();
      else phase_ = Phase::Cleanup;
      return;
    }
  }
  Progress result = Progress::Failed;
  bool forced = true;
  switch (phase_) {
  case Phase::Stop: result = operations_.internalReplacementStop(normal_); break;
  case Phase::Closed: result = operations_.waitAClosed(normal_); break;
  case Phase::Wait: result = operations_.waitAAndChildren(normal_, forced); break;
  case Phase::Launch: result = operations_.launchB(evidence_.grant, commitment_, normal_); break;
  case Phase::Transfer: result = operations_.transferToB(frame_, normal_); break;
  case Phase::Accepted: result = operations_.waitBGrantAccepted(normal_); break;
  case Phase::Permit:
    result = operations_.issueRestorePermit(normal_, [this] {
      return !canceled_ && withinNormal() && operations_.eligibleForB(evidence_.grant) && !canceled_ && withinNormal();
    });
    break;
  case Phase::Monitor: result = operations_.monitorB(normal_); break;
  default: return;
  }
  if (canceled_) { if (cancellation_ == Cancellation::Pending) stepCancellation(); return; }
  if (!withinNormal() || result == Progress::Failed || (phase_ == Phase::Wait && result == Progress::Complete && forced)) {
    if (bLaunchStarted_) { cancel(); stepCancellation(); }
    else phase_ = Phase::Cleanup;
    return;
  }
  if (result == Progress::Pending) return;
  if (phase_ == Phase::Stop) phase_ = Phase::Closed;
  else if (phase_ == Phase::Closed) phase_ = Phase::Wait;
  else if (phase_ == Phase::Wait) {
    if (!operations_.eligibleForB(evidence_.grant) || canceled_ || !withinNormal()) { phase_ = Phase::Cleanup; return; }
    frame_ = encodeGrant(evidence_.grant);
    if (frame_.isEmpty()) { phase_ = Phase::Cleanup; return; }
    commitment_ = QCryptographicHash::hash(frame_, QCryptographicHash::Sha256);
    burned_ = true;
    bLaunchStarted_ = true;
    phase_ = Phase::Launch;
  } else if (phase_ == Phase::Launch) phase_ = Phase::Transfer;
  else if (phase_ == Phase::Transfer) phase_ = Phase::Accepted;
  else if (phase_ == Phase::Accepted) phase_ = Phase::Permit;
  else if (phase_ == Phase::Permit) phase_ = Phase::Monitor;
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
  Q_UNUSED(channel); Q_UNUSED(frame); Q_UNUSED(expected); Q_UNUSED(now); Q_UNUSED(eof);
  attempted_ = true;
  return false;
}

bool ChildGate::accept(const void* channel, const QByteArray& frame, const Grant& expected,
                       const QByteArray& expectedDigest, qint64 now, bool eof)
{
  if (attempted_ || canceled_) return false;
  attempted_ = true;
  if (!channel_ || channel != channel_ || !eof || now < 0 || deadline_ <= now || frame.size() > 9216 || expectedDigest.size() != 32 ||
      QCryptographicHash::hash(frame, QCryptographicHash::Sha256) != expectedDigest) return false;
  accepted_ = decodeGrant(frame, expected, grant_);
  if (accepted_) acceptedDigest_ = expectedDigest;
  return accepted_;
}

bool ChildGate::accept(const void* channel, const QByteArray& frame, quint64 expectedRun,
                       const QByteArray& expectedDigest, qint64 now, bool eof)
{
  if (attempted_ || canceled_) return false;
  attempted_ = true;
  if (!channel_ || channel != channel_ || !eof || now < 0 || deadline_ <= now) return false;
  accepted_ = decodeCommittedGrant(frame, expectedRun, expectedDigest, grant_);
  if (accepted_) acceptedDigest_ = expectedDigest;
  return accepted_;
}

bool ChildGate::installPermit(const QByteArray& frame, const RestorePermit& expected, qint64 now)
{
  if (!accepted_ || canceled_ || permit_ || now < 0 || now >= deadline_ || expected.run != grant_.run ||
      expected.grant != grant_.grant || expected.revision != grant_.revision || expected.digest != acceptedDigest_) return false;
  RestorePermit parsed;
  if (!decodeRestorePermit(frame, expected, parsed)) return false;
  try { permit_ = std::make_shared<const RestorePermit>(std::move(parsed)); }
  catch (...) { return false; }
  return true;
}

bool ChildGate::allowsRestore(quint64 run, quint64 launch, quint64 grant, quint64 revision,
                              const QByteArray& digest, qint64 now) const
{
  return accepted_ && !canceled_ && permit_ && now >= 0 && now < deadline_ && permit_->run == run &&
         permit_->launch == launch && permit_->grant == grant && permit_->revision == revision &&
         permit_->digest == digest && grant_.run == run && grant_.grant == grant && grant_.revision == revision;
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
