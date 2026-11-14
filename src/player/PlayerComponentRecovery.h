#pragma once
#ifdef MPVQT_RECOVERY_RUNTIME
#include "AutomaticRecoveryFrameGate.h"
#include "AutomaticRecoveryLoad.h"
#include "AutomaticRecoveryObservation.h"
#include "AutomaticRecoveryPolicy.h"
#include "AutomaticRecoveryRuntime.h"
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <memory>
#include <mpv/client.h>

class PlayerComponent;
class MpvVideoItem;
namespace RendererRuntime { struct CoreOwner; }

// Internal control-thread coordinator; unavailable capability never starts recovery.
class PlayerComponentRecovery final : public QObject {
public:
  explicit PlayerComponentRecovery(PlayerComponent* player);
  ~PlayerComponentRecovery() override;
  bool bind();
  void shutdown();
  void drain();
  bool queue(const QStringList& command, const QString& url, const QVariantMap& options,
             const QVariantMap& metadata, const QVariant& audio, const QVariant& subtitle);
  void userChanged(bool clear);
  bool ownsEnd(const mpv_event* event) const;
  bool restoring() const { return restoring_; }
  bool recoveryPreloadAllowed() const;
private:
  void tick();
  void event(const mpv_event* event, std::int64_t now);
  void usability(std::uint64_t generation, std::uint64_t request, bool usable);
  void beginObservation(AutomaticRecoverySession::Key key, std::int64_t entry, std::int64_t now);
  bool sameIntent() const;
  void beginFrameGate(std::int64_t now);
  void rendered(std::uint64_t generation, const RecoveryFrameObservationV1& frame);
  void submitted(std::uint64_t generation, std::uint64_t serial);
  void swapped(std::uint64_t generation, std::uint64_t serial);
  void maybeResume(std::int64_t now);
  bool validRestore(std::int64_t now) const;
  void failRestore();
  void restoreEvent(const mpv_event* event, std::int64_t now);
  void dispatchReload(std::int64_t now);
  void recoveryTick(std::int64_t now);
  void recordSourceFrame(std::uint64_t generation, const RecoveryFrameObservationV1& frame);
  static std::int64_t nowMs();
  PlayerComponent* player_;
  QPointer<MpvVideoItem> item_;
  std::shared_ptr<RendererRuntime::CoreOwner> core_;
  QTimer timer_;
  AutomaticRecoveryRuntime runtime_;
  AutomaticRecoveryObservation observation_;
  AutomaticRecovery::Policy policy_;
  AutomaticRecoverySession::Snapshot saved_;
  AutomaticRecoveryLoad recoveryLoad_;
  AutomaticRecoveryFrameGate frameGate_;
  std::uint64_t session_ = 0, attempt_ = 0, request_ = 0;
  std::uint64_t rendererGeneration_ = 0, frameEpoch_ = 0, previousSource_ = 0, unpauseId_ = 0;
  std::uint64_t pauseId_ = 0;
  std::uint64_t verifiedSource_ = 0;
  std::int64_t recoveryEntry_ = 0, observationStarted_ = 0;
  std::uint64_t lastSource_ = 0;
  std::uint64_t sourceGeneration_ = 0, sourceEntry_ = 0, sourceFirst_ = 0;
  std::uint64_t sourceEpoch_ = 0;
  AutomaticRecoveryObservation::Result observationResult_;
  bool recoveryLoaded_ = false, frameGateActive_ = false, positionReady_ = false, observationRecovery_ = false;
  bool bound_ = false, observationPending_ = false, awaitingUsability_ = false;
  bool restoring_ = false, loadDispatched_ = false, resumePending_ = false, completed_ = false;
  bool recoveryRestarted_ = false;
};
#endif
