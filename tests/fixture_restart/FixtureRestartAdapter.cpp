#include "FixtureRestartAdapter.h"
#include "player/PlayerComponent.h"
#include "player/PlaybackCheckpoint.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

namespace {
bool fixtureHash(const QString& path, const QByteArray& expected, QString& canonical)
{
  const QFileInfo info(path);
  if (!info.isAbsolute() || !info.isFile() || info.isSymLink() || info.size() <= 0 || info.size() > 16 * 1024 * 1024)
    return false;
  canonical = info.canonicalFilePath();
  QFile file(canonical);
  if (!file.open(QIODevice::ReadOnly)) return false;
  QCryptographicHash hash(QCryptographicHash::Sha256);
  return hash.addData(&file) && file.error() == QFile::NoError && hash.result().toHex() == expected;
}
}

FixtureRestartAdapter::FixtureRestartAdapter(PlayerComponent& player, FixtureRestart::ChildGate& gate,
                                             quint64 host, quint64 attempt)
  : player_(player), gate_(gate), host_(host), attempt_(attempt),
    expectedRevision_(player.m_playbackIntent.snapshot().revision)
{
}

bool FixtureRestartAdapter::arm()
{
  const auto* grant = gate_.acceptedGrant();
  if (armed_ || !grant || !gate_.allowed(grant->revision) || !host_ || !attempt_) return false;
  parentRevision_ = grant->revision;
  if (!player_.bindNativeReceiptHost(host_)) return false;
  PlayerComponent::RestoreGrant typed{grant->grant, grant->revision, expectedRevision_,
                                       grant->positionMs, grant->desiredPaused};
  armed_ = player_.armNativeRestore(typed, host_, attempt_);
  return armed_;
}

bool FixtureRestartAdapter::current() const
{
  return armed_ && gate_.allowed(parentRevision_);
}

bool FixtureRestartAdapter::load(const QString& resolvedFixturePath)
{
  const auto* grant = gate_.acceptedGrant();
  QString canonical;
  if (!current() || !grant || !fixtureHash(resolvedFixturePath, grant->assetHash, canonical)) return false;
  return current() && player_.dispatchNativeRestoreLoad(canonical);
}

bool FixtureRestartAdapter::seek()
{
  return current() && player_.dispatchNativeRestoreSeek();
}

bool FixtureRestartAdapter::checkedStop(void* context)
{
  auto& self = *static_cast<FixtureRestartAdapter*>(context);
  if (!self.armed_) return true;
  const bool attempted = self.player_.m_nativeRestore.loadAttempted;
  const auto result = self.player_.cancelNativeRestore();
  return self.player_.m_nativeRestore.cancelled &&
      (!attempted || result.status == PlayerComponent::NativeStopResult::Status::Accepted);
}

bool FixtureRestartAdapter::cancel()
{
  return gate_.cancel(checkedStop, this);
}

bool FixtureRestartAdapter::loadA(const QString& path, const QByteArray& expectedHash, bool paused)
{
  QString canonical;
  if (aLoad_ || armed_ || !host_ || !attempt_ || !player_.intentControlThread() ||
      player_.m_playbackIntent.snapshot().revision != expectedRevision_ || !fixtureHash(path, expectedHash, canonical)) return false;
  if (!player_.bindNativeReceiptHost(host_) || player_.m_playbackIntent.snapshot().revision != expectedRevision_) return false;
  player_.m_playbackIntent.play(paused);
  aLoad_ = player_.beginNativeReceipt(PlayerComponent::FixtureAsset::TestClip, canonical, host_, attempt_, paused);
  if (aLoad_) { aPath_ = canonical; aHash_ = expectedHash; }
  return aLoad_;
}

bool FixtureRestartAdapter::captureA(const FixtureRestart::Grant& binding, FixtureRestart::Evidence& output)
{
  auto& player = player_;
  QString canonical;
  if (!aLoad_ || binding.assetHash != aHash_ || !fixtureHash(aPath_, aHash_, canonical) || canonical != aPath_) return false;
  if (!player.intentControlThread() || !player.nativeObservationCurrent()) return false;
  const auto receipt = player.m_nativeReceipts.snapshot();
  const auto observation = player.m_nativeObservation;
  const auto intent = player.m_playbackIntent.snapshot();
  if (receipt.key.attempt != attempt_ || receipt.key.host != host_ || observation.positionMs < 0 || observation.positionMs >= PlayerComponent::FixtureDurationMs ||
      (intent.desired != PlaybackIntent::Desired::Playing && intent.desired != PlaybackIntent::Desired::Paused) ||
      observation.paused != (intent.desired == PlaybackIntent::Desired::Paused)) return false;
  QByteArray checkpoint;
  if (!PlaybackCheckpointCodec::encode(player.makePlaybackCheckpoint(), checkpoint)) return false;
  QJsonObject receiptData{{"host", QString::number(receipt.key.host)}, {"core", QString::number(receipt.key.core)},
    {"revision", QString::number(receipt.key.revision)}, {"attempt", QString::number(receipt.key.attempt)},
    {"entry", QString::number(receipt.entry)}};
  QJsonObject observed{{"id", QString::number(observation.id)}, {"requestedNs", QString::number(observation.requestedNs)},
    {"receivedNs", QString::number(observation.receivedNs)}, {"positionMs", QString::number(observation.positionMs)},
    {"paused", observation.paused}};
  FixtureRestart::Evidence result;
  result.receipt = QJsonDocument(receiptData).toJson(QJsonDocument::Compact);
  result.observation = QJsonDocument(observed).toJson(QJsonDocument::Compact);
  result.checkpoint = checkpoint;
  result.sourceAttempt = receipt.key.attempt;
  result.grant = binding;
  result.grant.grant = 0;
  result.grant.positionMs = observation.positionMs;
  result.grant.desiredPaused = observation.paused;
  output = result;
  return true;
}

bool FixtureRestartAdapter::internalReplacementStop(const FixtureRestart::Evidence& frozen)
{
  auto& player = player_;
  FixtureRestart::Evidence current;
  if (!captureA(frozen.grant, current) || current.receipt != frozen.receipt ||
      current.observation != frozen.observation || current.checkpoint != frozen.checkpoint) return false;
  const auto revision = player.m_playbackIntent.snapshot().revision;
  player.invalidateNativeReceipt();
  const auto result = player.stopMpv();
  return result.status == PlayerComponent::NativeStopResult::Status::Accepted &&
      player.m_playbackIntent.snapshot().revision == revision;
}
