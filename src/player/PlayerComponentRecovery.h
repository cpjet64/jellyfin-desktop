#pragma once
#ifdef MPVQT_RECOVERY_RUNTIME
#include "AutomaticRecoveryFrameGate.h"
#include "AutomaticRecoveryLoad.h"
#include "AutomaticRecoveryObservation.h"
#include "AutomaticRecoveryPolicy.h"
#include "AutomaticRecoveryReadStall.h"
#include "AutomaticRecoveryRuntime.h"
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <memory>
#include <mpv/client.h>
#include "RendererOwners.h"

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
  bool prepareRecoveryPreload();
  bool preparePreload(const QString& hookToken);
  void finishPreload();
  void applyDeferredTracks();
  void beginUserReplacement() { replacedEntry_ = runtime_.activeEntry(); }
  RendererRuntime::NativeLease acquireNative() const;
  std::shared_ptr<RendererRuntime::CoreOwner> coreOwner() const { return core_; }
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
  void watchdogObservation(AutomaticRecoverySession::Key key, const AutomaticRecoveryObservation::Result& result,
                           std::int64_t now);
  bool qualifiedReadStall(AutomaticRecoverySession::Key key, const AutomaticRecoveryObservation::Result& result,
                          std::int64_t now, bool movingVideo);
  void recordSourceFrame(std::uint64_t generation, const RecoveryFrameObservationV1& frame);
  static std::int64_t nowMs();
  PlayerComponent* player_;
  QPointer<MpvVideoItem> item_;
  std::shared_ptr<RendererRuntime::CoreOwner> core_;
  RendererRuntime::NativeLease lifetimeLease_;
  QTimer timer_;
  AutomaticRecoveryRuntime runtime_;
  AutomaticRecoveryObservation observation_;
  AutomaticRecovery::Policy policy_;
  AutomaticRecoveryReadStall readStall_;
  AutomaticRecoverySession::Snapshot saved_;
  AutomaticRecoveryLoad recoveryLoad_;
  AutomaticRecoveryFrameGate frameGate_;
  std::uint64_t session_ = 0, attempt_ = 0, request_ = 0;
  std::uint64_t rendererGeneration_ = 0, frameEpoch_ = 0, previousSource_ = 0, unpauseId_ = 0;
  std::uint64_t pauseId_ = 0;
  std::uint64_t verifiedSource_ = 0;
  std::int64_t recoveryEntry_ = 0, observationStarted_ = 0;
  std::int64_t replacedEntry_ = 0;
  std::int64_t lastStartedEntry_ = 0;
  std::int64_t transitionDeadline_ = 0;
  std::uint64_t lastSource_ = 0;
  std::uint64_t sourceGeneration_ = 0, sourceEntry_ = 0, sourceFirst_ = 0;
  std::uint64_t sourceEpoch_ = 0;
  std::uint64_t startSerial_ = 0, hookStart_ = 0, hookRevision_ = 0, hookBinding_ = 0, hookAttempt_ = 0;
  std::uint64_t deferredStart_ = 0, deferredRevision_ = 0, deferredBinding_ = 0, deferredAttempt_ = 0;
  std::int64_t hookEntry_ = 0, deferredEntry_ = 0;
  std::int64_t cancelledRecoveryEntry_ = 0;
  QString hookToken_;
  bool hookTokenValid_ = false;
  AutomaticRecoveryObservation::Result observationResult_;
  bool recoveryLoaded_ = false, frameGateActive_ = false, positionReady_ = false, observationRecovery_ = false;
  bool bound_ = false, observationPending_ = false, awaitingUsability_ = false;
  bool restoring_ = false, loadDispatched_ = false, resumePending_ = false, completed_ = false;
  bool recoveryRestarted_ = false;
  bool recoveryTracksApplied_ = false;
  bool replacementTransition_ = false;
};
#endif
