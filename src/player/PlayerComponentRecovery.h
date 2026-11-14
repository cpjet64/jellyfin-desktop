#pragma once
#ifdef MPVQT_RECOVERY_RUNTIME
#include "AutomaticRecoveryFrameGate.h"
#include "AutomaticRecoveryLoad.h"
#include "AutomaticRecoveryObservation.h"
#include "AutomaticRecoveryOwnedSeek.h"
#include "AutomaticRecoveryPolicy.h"
#include "AutomaticRecoveryReadStall.h"
#include "AutomaticRecoveryRuntime.h"
#include "OrdinaryLoadSelection.h"
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
  bool userChanged(bool clear);
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
  void prepareOwnedSeek(std::int64_t targetMs);
  int dispatchOwnedSeek(std::int64_t targetMs);
  bool cancelBeforeIntent() { return cancelOwnedSeek(); }
private:
  void tick();
  void event(const mpv_event* event, std::int64_t now);
  void usability(std::uint64_t generation, std::uint64_t request, bool usable);
  void beginObservation(AutomaticRecoverySession::Key key, std::int64_t entry, std::int64_t now);
  bool sameIntent() const;
  void beginFrameGate(std::int64_t now);
  void rendered(std::uint64_t generation, const RecoveryFrameObservationV1& frame);
  void submission(std::uint64_t generation, const QtRecoverySubmissionV1& submission);
  void acceptBootstrapSubmission();
  bool armFrameAttempt();
  void clearFrameAttempt();
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
  void checkOwnedSeek(std::int64_t now);
  bool cancelOwnedSeek();
  void seekReply(const mpv_event* event, std::int64_t now);
  void seekPosition(const AutomaticRecoveryObservation::Result& result, std::int64_t now);
  void seekSubmission(const AutomaticRecoveryOwnedSeek::Submission& submission, std::int64_t now);
  void seekQtSubmission(std::uint64_t generation, const QtRecoverySubmissionV1& submission, std::uint64_t operationId);
  void beginSeekRecovery(AutomaticRecoveryOwnedSeek::State reason, std::int64_t now);
  void ordinaryEvent(const mpv_event* event, std::int64_t now);
  bool beginOrdinaryLoad(const QStringList& command, const QString& url, const QVariantMap& options,
                         const QVariantMap& metadata, const QVariant& audio, const QVariant& subtitle,
                         AutomaticRecoverySession::Key key);
  bool flushOrdinaryLoad(bool allowSelection);
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
  AutomaticRecoveryOwnedSeek ownedSeek_;
  OrdinaryLoadSelection ordinarySelection_;
  struct DeferredOrdinaryLoad {
    AutomaticRecoverySession::Key key;
    QStringList command;
    QString url;
    QVariantMap options, metadata;
    QVariant audio, subtitle;
    std::uint64_t queryId = 0, beforeStart = 0;
    std::int64_t deadlineMs = 0;
    bool pending = false;
  } ordinaryLoad_;
  QSet<std::int64_t> ordinaryBeforeEntries_;
  QString ordinaryExpectedUrl_;
  std::uint64_t ordinaryAfterId_ = 0, ordinaryAfterStart_ = 0, ordinaryAfterRevision_ = 0, ordinaryAfterBinding_ = 0;
  std::int64_t ordinaryAfterEntry_ = 0;
  std::uint64_t ordinaryUnresolvedStarts_ = 0;
  bool ordinaryCounterValid_ = true;
  AutomaticRecoverySession::Snapshot seekCheckpoint_;
  AutomaticRecoveryObservation::Result lastObservation_;
  AutomaticRecoveryObservation::Result seekPriorObservation_;
  AutomaticRecoverySession::Snapshot saved_;
  AutomaticRecoveryLoad recoveryLoad_;
  AutomaticRecoveryFrameGate frameGate_;
  std::uint64_t session_ = 0, attempt_ = 0, request_ = 0;
  std::uint64_t rendererGeneration_ = 0, frameEpoch_ = 0, previousSource_ = 0, unpauseId_ = 0;
  std::uint64_t windowLifetime_ = 0, sceneGeneration_ = 0, pendingSubmissionEpoch_ = 0;
  QtRecoverySubmissionV1 pendingSubmission_{};
  QtRecoverySubmissionV1 pendingBootstrap_{};
  std::uint64_t pendingBootstrapEpoch_ = 0;
  std::uint64_t pauseId_ = 0;
  std::uint64_t verifiedSource_ = 0;
  std::int64_t recoveryEntry_ = 0, observationStarted_ = 0;
  std::int64_t replacedEntry_ = 0;
  std::int64_t lastStartedEntry_ = 0;
  std::int64_t transitionDeadline_ = 0;
  std::uint64_t lastSource_ = 0;
  std::uint64_t sourceGeneration_ = 0, sourceEntry_ = 0, sourceFirst_ = 0;
  std::uint64_t sourceDiscontinuity_ = 0, seekOperationSequence_ = 0, seekReplySequence_ = 0;
  std::uint64_t bootstrapEntry_ = 0, bootstrapSource_ = 0, bootstrapDiscontinuity_ = 0;
  std::uint64_t bootstrapGeneration_ = 0, bootstrapEpoch_ = 0, bootstrapWindow_ = 0, bootstrapScene_ = 0;
  std::uint64_t seekBaselineSource_ = 0, seekBaselineDiscontinuity_ = 0;
  std::uint64_t seekWindowLifetime_ = 0, seekSceneGeneration_ = 0;
  AutomaticRecoveryOwnedSeek::State seekTimeoutReason_ = AutomaticRecoveryOwnedSeek::State::Empty;
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
  bool submissionPending_ = false;
  bool bootstrapPending_ = false;
  bool recoveryTracksApplied_ = false;
  bool replacementTransition_ = false;
  bool seekPrepared_ = false, nativeSeekArmed_ = false, seekCancellationFailed_ = false;
};
#endif
