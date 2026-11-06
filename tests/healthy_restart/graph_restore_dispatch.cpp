#include "graph.h"
#include "graph_session.h"
#include "capture_capability.h"
#include "module_endpoints.h"
#include "player/PlayerComponent.h"
#include <QThread>
#include <algorithm>
#include <chrono>

namespace HealthyRetirement {
bool FixtureGraph::restoreLaunch(HealthyModuleIdentity::LaunchBinding& binding, std::uint64_t& launch) const
{
#ifdef HEALTHY_RETIREMENT_TESTING
  if (restoreBootstrapForTest_) {
    const auto test = restoreBootstrapForTest_;
    try {
      if (!test->accepted || test->storage.expired() || !test->moduleLive || !test->moduleLive() || test->storage.expired()) return false;
      binding = test->binding; launch = test->launch;
      return true;
    } catch (...) { return false; }
  }
#endif
  return module_ && slot_ && module_->stillValid(slot_->generation()) &&
      module_->launchBinding(slot_->generation(), binding) && module_->launchId(slot_->generation(), launch);
}

const void* FixtureGraph::restoreToken(HealthyModuleIdentity::EndpointRole role) const
{
#ifdef HEALTHY_RETIREMENT_TESTING
  if (restoreBootstrapForTest_) {
    const auto test = restoreBootstrapForTest_;
    try {
      if (!test->accepted || test->storage.expired() || !test->endpointsLive || !test->endpointsLive() || test->storage.expired()) return nullptr;
      using HealthyModuleIdentity::EndpointRole;
      if (role == EndpointRole::Grant) return test->grant;
      if (role == EndpointRole::Control) return test->control;
      return role == EndpointRole::Ack ? test->ack : nullptr;
    } catch (...) { return nullptr; }
  }
#endif
  return endpoints_ ? endpoints_->token(role) : nullptr;
}

#ifdef HEALTHY_RETIREMENT_TESTING
bool FixtureGraph::installRestoreBootstrapForTest(HealthyModuleIdentity::LaunchBinding binding, std::uint64_t launch,
    const void* grant, const void* control, const void* ack, std::weak_ptr<void> storage,
    std::function<bool()> moduleLive, std::function<bool()> endpointsLive,
    std::weak_ptr<const std::atomic_bool> moduleState, std::weak_ptr<const std::atomic_bool> endpointState,
    std::weak_ptr<const std::atomic<qint64>> clockState)
{
  if (restoreBootstrapForTest_ || session_ || !dispatcher_ || dispatcher_->thread() != QThread::currentThread() ||
      !access_ || access_->revoked.load() || access_->phase.load() != AccessPhase::Normal ||
      binding.role != HealthyModuleIdentity::LaunchRole::Restore || !binding.run || !launch ||
      !grant || !control || !ack || grant == control || grant == ack || control == ack || storage.expired() ||
      !moduleLive || !endpointsLive || moduleState.expired() || endpointState.expired() || clockState.expired()) return false;
  const auto digestSet = std::any_of(binding.expectedGrantDigest.begin(), binding.expectedGrantDigest.end(),
                                     [](unsigned char value) { return value != 0; });
  if (!digestSet) return false;
  try {
    auto test = std::make_shared<TestBootstrap>();
    test->binding = binding; test->launch = launch; test->grant = grant; test->control = control; test->ack = ack;
    test->storage = std::move(storage); test->moduleLive = std::move(moduleLive);
    test->endpointsLive = std::move(endpointsLive);
    test->moduleState = std::move(moduleState); test->endpointState = std::move(endpointState);
    test->clockState = std::move(clockState);
    restoreBootstrapForTest_ = test;
    const auto now = clock_();
    if (now < 0 || now >= normalDeadline_ || test->storage.expired() || !test->moduleLive() ||
        !test->endpointsLive() || test->storage.expired()) return false;
    const auto after = clock_();
    const auto retainedModuleState = test->moduleState.lock();
    const auto retainedEndpointState = test->endpointState.lock();
    const auto retainedClockState = test->clockState.lock();
    if (after < now || after >= normalDeadline_ || test->storage.expired() ||
        !retainedModuleState || !retainedEndpointState || !retainedClockState || !retainedModuleState->load() ||
        !retainedEndpointState->load() || retainedClockState->load() < after ||
        retainedClockState->load() >= normalDeadline_ ||
        access_->phase.load() != AccessPhase::Normal || access_->revoked.load()) return false;
    test->accepted = true;
    return true;
  } catch (...) { return false; }
}
#endif

bool FixtureGraph::bindRestoreAuthority()
{
  if (!sessionReady(false) || !session_ || !session_->gate || !player_ || !slot_ || !capture_ ||
      !capturePlayerBound() || !player_->m_restoreGate || player_->m_restoreGate != session_->gate) return false;
  const auto key = player_->m_nativeRestore.key;
  const auto claim = std::atomic_load(&access_->intentClaim);
  const RetirementClaim::ArmIdentity identity{key.host, key.core, key.revision, key.attempt};
  if (!claim || claim != player_->m_healthyStopLease || !claim->open() ||
      !claim->matchesArm(slot_->generation(), identity) || key.host != session_->host ||
      key.core != session_->core || key.revision != session_->intentRevision || key.attempt != session_->attempt)
    return false;
  if (player_->m_healthyRestoreAuthority) {
    const auto bound = player_->m_healthyRestoreAuthorityKey;
    return bound.host == key.host && bound.core == key.core && bound.revision == key.revision &&
        bound.attempt == key.attempt;
  }
  const std::weak_ptr<GraphAccess> access = access_;
  const std::weak_ptr<HealthyModuleIdentity::ChildEndpoints> endpoints = endpoints_;
  const std::weak_ptr<const HealthyModuleIdentity::VerifiedModule> module = module_;
  const std::weak_ptr<const FixtureRestart::ChildGate> gate = session_->gate;
  const std::weak_ptr<RetirementClaim> weakClaim = claim;
  const std::weak_ptr<CaptureController> capture = capture_;
  const auto generation = slot_->generation();
  const auto run = session_->run, launch = session_->launch, grant = session_->binding.grant;
  const auto revision = session_->binding.revision;
  const auto digest = session_->digest;
  const auto deadline = normalDeadline_;
  const auto clock = clock_;
  try {
    std::function<bool()> transport;
#ifdef HEALTHY_RETIREMENT_TESTING
    if (restoreBootstrapForTest_) {
      const std::weak_ptr<const TestBootstrap> bootstrap = restoreBootstrapForTest_;
      transport = [bootstrap] {
        const auto b = bootstrap.lock();
        return b && b->accepted && !b->storage.expired() && b->moduleLive() && b->endpointsLive() &&
            b->moduleLive() && b->endpointsLive() && !b->storage.expired();
      };
    } else
#endif
    transport = [module, endpoints, generation] {
      const auto m = module.lock(); const auto e = endpoints.lock();
      return m && e && m->stillValid(generation) && e->token(HealthyModuleIdentity::EndpointRole::Control) &&
          e->token(HealthyModuleIdentity::EndpointRole::Ack) && m->stillValid(generation) &&
          e->token(HealthyModuleIdentity::EndpointRole::Control) && e->token(HealthyModuleIdentity::EndpointRole::Ack);
    };
    const auto boundAt = clock();
    if (boundAt < 0 || boundAt >= deadline || !sessionReady(false) || !claim->open()) return false;
    const auto last = std::make_shared<qint64>(boundAt);
    player_->m_healthyRestoreAuthority = [access, gate, weakClaim, capture, transport,
        generation, run, launch, grant, revision, digest, deadline, clock, identity, last](bool seeking) {
      const auto a = access.lock();
      const auto g = gate.lock(); const auto c = weakClaim.lock(); const auto frame = capture.lock();
      if (!a || !g || !c || !frame || a->guiThread != std::this_thread::get_id() ||
          a->guiQtThread != QThread::currentThread() || a->revoked.load() ||
          a->phase.load() != AccessPhase::Normal || !a->graph.load() ||
          std::atomic_load(&a->intentClaim) != c || !c->open() || !c->matchesArm(generation, identity)) return false;
      const auto now = clock();
      if (now < *last || now >= deadline || !g->allowsRestore(run, launch, grant, revision, digest, now) ||
          !transport() || (seeking && !frame->ready(identity.attempt, CaptureBridge::Phase::Target)) ||
          !transport()) return false;
      if (now < *last) return false;
      *last = now;
      return !a->revoked.load() && a->phase.load() == AccessPhase::Normal && a->graph.load() &&
          std::atomic_load(&a->intentClaim) == c && c->open() && c->matchesArm(generation, identity) &&
          g->allowsRestore(run, launch, grant, revision, digest, now);
    };
    player_->m_healthyRestoreAuthorityKey = key;
    if (bindRestoreFinal(claim, identity)) return true;
    player_->m_healthyRestoreAuthority = {};
    return false;
  } catch (...) { player_->m_healthyRestoreAuthority = {}; return false; }
}

bool FixtureGraph::bindRestoreFinal(const std::shared_ptr<RetirementClaim>& claim,
                                     RetirementClaim::ArmIdentity identity)
{
  if (!player_ || !slot_ || !capture_ || !claim || !claim->open()) return false;
  const auto generation = slot_->generation();
  const auto control = restoreToken(HealthyModuleIdentity::EndpointRole::Control);
  const auto ack = restoreToken(HealthyModuleIdentity::EndpointRole::Ack);
  if (!control || !ack || control == ack || !slot_->bindStopClaim(claim)) return false;
#ifdef HEALTHY_RETIREMENT_TESTING
  const bool testMode = bool(restoreBootstrapForTest_);
  const std::weak_ptr<const TestBootstrap> bootstrap = restoreBootstrapForTest_;
  if (!testMode)
#endif
  {
    if (!module_ || !endpoints_ || !module_->bindAdmissionClaim(claim, generation) ||
        !endpoints_->bindAdmissionClaim(claim, generation)) return false;
  }
  const std::weak_ptr<GraphAccess> access = access_;
  const std::weak_ptr<Containment> containment = containment_;
  const std::weak_ptr<const HealthyModuleIdentity::VerifiedModule> module = module_;
  const std::weak_ptr<HealthyModuleIdentity::ChildEndpoints> endpoints = endpoints_;
  const std::weak_ptr<RetirementClaim> weakClaim = claim;
  const auto deadline = normalDeadline_;
  const auto last = std::make_shared<std::atomic<qint64>>(0);
  player_->m_healthyRestoreFinalSnapshot = [access, containment, module, endpoints, weakClaim,
#ifdef HEALTHY_RETIREMENT_TESTING
      bootstrap, testMode,
#endif
      generation, control, ack, deadline, last](qint64& output) {
    const auto a = access.lock(); const auto held = containment.lock(); const auto c = weakClaim.lock();
    if (!a || !held || !c || !held->intact() || a->revoked.load() || a->phase.load() != AccessPhase::Normal ||
        !a->graph.load() || std::atomic_load(&a->intentClaim) != c || !c->open()) return false;
    qint64 now = -1;
#ifdef HEALTHY_RETIREMENT_TESTING
    if (testMode) {
      const auto b = bootstrap.lock();
      const auto moduleState = b ? b->moduleState.lock() : nullptr;
      const auto endpointState = b ? b->endpointState.lock() : nullptr;
      const auto clockState = b ? b->clockState.lock() : nullptr;
      if (!b || !b->accepted || b->storage.expired() || !moduleState || !endpointState || !clockState ||
          !moduleState->load() || !endpointState->load() || b->control != control || b->ack != ack) {
        c->revoke(); return false;
      }
      now = clockState->load();
    } else
#endif
    {
      const auto m = module.lock(); const auto e = endpoints.lock();
      if (!m || !e || !m->admissionSnapshot(generation) || !e->admissionSnapshot(generation, control, ack)) {
        c->revoke(); return false;
      }
      now = std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    if (now < 0 || now < last->load() || now >= deadline) { c->revoke(); return false; }
    last->store(now);
    if (!held->intact() || a->revoked.load() || a->phase.load() != AccessPhase::Normal || !a->graph.load() ||
        std::atomic_load(&a->intentClaim) != c || !c->open()) return false;
    output = now;
    return true;
  };
  player_->m_healthyRestoreCapture = capture_;
  player_->m_healthyRestoreSlot = slot_;
  player_->m_healthyRestoreGeneration = generation;
  return claim->matchesArm(generation, identity) && claim->open();
}

bool FixtureGraph::prepareRestoreAuthority()
{
  if (!sessionReady(false) || !session_ || !session_->gate || !player_) return false;
  const auto& binding = session_->binding;
  if (!player_->m_restoreGate && !player_->healthyBindRestoreGate(session_->gate, binding.run,
      session_->launch, binding.grant, binding.revision, session_->digest, clock_)) return false;
  return sessionReady(false) && bindRestoreAuthority();
}

bool FixtureGraph::requestRestoreObservation()
{
  if (!player_ || !player_->healthyRestoreSubmissionAllowed(false) ||
      player_->m_lastObservationId >= PlaybackIntent::MaxSafeInteger) return false;
  const auto key = player_->m_nativeRestore.key;
  const auto id = player_->m_lastObservationId + 1;
  if (!player_->requestNativeObservation(key, id, PlayerComponent::FixtureDurationMs, true)) return false;
  return sessionReady(false) && player_->healthyRestoreSubmissionAllowed(false);
}

bool FixtureGraph::restoreProofReady()
{
  if (!sessionReady(false) || !capturePlayerBound() || !player_ || !capture_ ||
      !player_->m_restorePausedProof || !player_->restorePostSeekReady() ||
      !player_->nativePostSeekObservationCurrent() || !player_->healthyUnpauseAuthorized(true) ||
      !player_->m_healthyRestoreFinalSnapshot) return false;
  const auto claim = player_->m_healthyStopLease;
  const auto key = player_->m_nativeRestore.key;
  qint64 now = -1;
  const auto snapshot = player_->m_healthyRestoreFinalSnapshot;
  if (!snapshot(now) || now < player_->m_restoreGateLast || !claim || claim != player_->m_healthyStopLease ||
      !claim->open() || !player_->restoreOwnsCurrent() || !player_->m_restoreGate ||
      !player_->m_restoreGate->allowsRestore(player_->m_restoreRun, player_->m_restoreLaunch,
          player_->m_restoreGrant, player_->m_restoreRevision, player_->m_restoreDigest, now) ||
      capture_->status(key.attempt, CaptureBridge::Phase::Target) != CaptureBridge::Status::Ready) return false;
  return claim->open() && player_->restoreOwnsCurrent() && !player_->m_nativeRestore.cancelled &&
      !player_->m_nativeRestore.failed;
}

FixtureGraph::PlaybackProgress FixtureGraph::pollRestorePlayback()
{
  using Progress = PlaybackProgress;
  try {
  if (!sessionReady(false) || !session_ || !session_->gate || !session_->child || !player_) return Progress::Failed;
  const auto now = clock_();
  if (!session_->gate->allowsRestore(session_->run, session_->launch, session_->binding.grant,
                                     session_->binding.revision, session_->digest, now))
    return session_->gate->canceled() ? Progress::Failed : Progress::Pending;
  if (!player_->m_nativeRestore.loadAttempted) return restoreLoad() ? Progress::Pending : Progress::Failed;
  if (!player_->healthyRestoreSubmissionAllowed(false)) return Progress::Failed;
  const auto receipt = player_->m_nativeReceipts.snapshot();
  if (receipt.invalid || receipt.ended || (receipt.submissionObserved && !receipt.submitted) ||
      (receipt.reply && !receipt.replyOk)) return Progress::Failed;
  if (!receipt.reply || !receipt.started) return Progress::Pending;
  auto& observation = player_->m_nativeObservation;
  if (!player_->m_nativeRestore.seekAttempted) {
    if (observation.pending) return Progress::Pending;
    if (!observation.id) return requestRestoreObservation() ? Progress::Pending : Progress::Failed;
    if (!player_->nativeObservationCurrent()) return Progress::Failed;
    using CaptureStatus = CaptureBridge::Status;
    if (!captureBaselineRequested_) {
      if (captureBaseline()) return Progress::Pending;
      return capture_->availability() == CaptureStatus::Pending ? Progress::Pending : Progress::Failed;
    }
    const auto baseline = capture_->status(receipt.key.attempt, CaptureBridge::Phase::Baseline);
    if (baseline == CaptureStatus::Rejected) return Progress::Failed;
    if (baseline == CaptureStatus::Pending) return Progress::Pending;
    if (!captureTargetRequested_) {
      if (captureTarget()) return Progress::Pending;
      return player_->m_healthyCaptureRejected ? Progress::Failed : Progress::Pending;
    }
    const auto target = capture_->status(receipt.key.attempt, CaptureBridge::Phase::Target);
    if (target == CaptureStatus::Rejected) return Progress::Failed;
    if (target == CaptureStatus::Pending) return Progress::Pending;
    if (!captureTargetReady()) return Progress::Failed;
    return restoreSeek() ? Progress::Pending : Progress::Failed;
  }
  const auto& seek = player_->m_nativeRestore;
  if (seek.failed || (seek.seekSubmissionKnown && !seek.seekAccepted) ||
      (seek.seekReply && !seek.seekOk)) return Progress::Failed;
  if (player_->m_restorePausedProof)
    return restoreProofReady() ? Progress::Ready : Progress::Failed;
  if (!player_->healthyCapturePausedHold()) return Progress::Failed;
  if (!player_->restorePostSeekReady() || observation.pending) return Progress::Pending;
  if (!observation.postSeek) return requestRestoreObservation() ? Progress::Pending : Progress::Failed;
  if (!player_->nativePostSeekObservationCurrent()) return Progress::Failed;
  if (observation.id != restoreObservedId_) {
    if (!player_->healthyObservePausedHold()) return Progress::Failed;
    restoreObservedId_ = observation.id;
  }
  if (player_->healthyFinishPausedHold()) return restoreProofReady() ? Progress::Ready : Progress::Failed;
  if (now < observation.receivedNs) return Progress::Failed;
  constexpr qint64 sampleIntervalNs = 100000000;
  if (now - observation.receivedNs < sampleIntervalNs) return Progress::Pending;
  return requestRestoreObservation() ? Progress::Pending : Progress::Failed;
  } catch (...) { return Progress::Failed; }
}
}
