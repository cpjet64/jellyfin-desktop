#include "restore_test_fixture.h"
#include "restore_test_renderer.h"
#include "graph_session.h"
#include "frame_capture.h"
#include "fixture_restart/OwnedFixtureFile.h"
#include "fixture_restart/RestartWire.h"
#include "player/PlayerComponent.h"
#include "player_test_cache.h"
#include <QCryptographicHash>
#include <QTemporaryFile>
#include <chrono>
#include <cstring>
#include <limits>

using namespace HealthyRetirement;
using namespace FixtureRestart;
// Containment can retain a graph after teardown. Never admit another in this process.
namespace { std::atomic_flag graphEverPrepared = ATOMIC_FLAG_INIT; }

HealthyRestoreTestFixture::HealthyRestoreTestFixture()
  : tokens_(std::make_shared<Tokens>()), state_(std::make_shared<State>()) {}
HealthyRestoreTestFixture::~HealthyRestoreTestFixture()
{
  state_->moduleHook = {}; state_->endpointHook = {};
  state_->clockHook = {}; state_->submissionHook = {};
  if (prepared_) prepared_->invalidate();
  renderer_.reset(); prepared_.reset();
}

void HealthyRestoreTestFixture::initializeProcessCache()
{ static PlayerTestCache cache; }

qint64 HealthyRestoreTestFixture::now()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool HealthyRestoreTestFixture::initialize(bool desiredPaused, qint64 checkpointMs)
{ return prepare(desiredPaused, checkpointMs) && initializeRestore(); }

bool HealthyRestoreTestFixture::prepare(bool desiredPaused, qint64 checkpointMs)
{
  if (prepared_ || graphEverPrepared.test_and_set()) return false;
  initializeProcessCache();
  fixture_restart::disarmCapture();
  const auto start = now();
  if (start < 0 || start > std::numeric_limits<qint64>::max() - 40000000000LL) return false;
  const auto state = state_;
  const auto clock = [state] {
    ++state->clockCalls;
    if (!state->clockHookAt || state->clockCalls == state->clockHookAt) {
      auto hook = std::move(state->clockHook); if (hook) hook();
    }
    const auto current = now(), offset = state->clockOffset.load();
    if ((offset > 0 && current > std::numeric_limits<qint64>::max() - offset) ||
        (offset < 0 && current < std::numeric_limits<qint64>::min() - offset)) return qint64{-1};
    const auto sampled = current + offset;
    state->clockNs.store(sampled);
    return sampled;
  };
  prepared_ = FixtureGraph::prepare(9, start + 10000000000LL, start + 40000000000LL, clock);
  if (!prepared_ || !(graph_ = prepared_->graphOnGuiThread())) return false;
  graph_->containment_ = prepared_->containment();
  graph_->player_ = std::make_unique<PlayerComponent>();
  auto& player = *graph_->player_;
  player.m_mpvGeneration = 8;
  player.m_healthyStopClock = clock;
  player.m_observationDispatch = [](quint64, const char**) { return 0; };
  player.m_restoreDispatch = [state](quint64 userdata, const char** args) {
    state->lastUserdata = userdata;
    state->lastArguments.clear();
    for (int i = 0; args[i]; ++i) state->lastArguments.emplace_back(args[i]);
    const bool load = !std::strcmp(args[0], "loadfile");
    if (load) {
      ++state->loads;
      for (int i = 1; args[i]; ++i) if (!std::strcmp(args[i], "pause=yes")) state->pausedLoad = true;
    } else if (!std::strcmp(args[0], "seek")) ++state->seeks;
    else if (!std::strcmp(args[0], "stop")) ++state->stops;
    auto hook = std::move(state->submissionHook); if (hook) hook();
    return load ? state->loadResult : state->seekResult;
  };
  player.m_healthyStopDispatch = [state](quint64 id, const char** args, bool property) {
    if (property && !std::strcmp(args[0], "idle-active")) state->idleQuery = id;
    if (property && !std::strcmp(args[0], "playlist-pos")) state->positionQuery = id;
    return 0;
  };
  return installBootstrap(desiredPaused, checkpointMs) && installRenderer();
}

bool HealthyRestoreTestFixture::initializeRestore()
{ return graph_ && media_ && graph_->restoreInitialize(media_->fileName()); }

bool HealthyRestoreTestFixture::cleanupBindPending() const
{
  const auto* p = player();
  return gate() && p && !p->m_restoreCleanupGate && !p->m_nativeRestore.armed &&
      !p->m_nativeRestore.loadAttempted && !p->m_restoreCleanup.claimed;
}

bool HealthyRestoreTestFixture::installBootstrap(bool desiredPaused, qint64 checkpointMs)
{
  const auto path = ownedFixtureTemplate();
  if (path.isEmpty()) return false;
  media_ = std::make_unique<QTemporaryFile>(path);
  if (!media_->open()) return false;
  const QByteArray content("bounded headless fixture");
  if (media_->write(content) != content.size() || !media_->flush()) return false;
  const auto asset = QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex();
  grant_ = {1, 3, 4, asset, checkpointMs, desiredPaused};
  digest_ = QCryptographicHash::hash(encodeGrant(grant_), QCryptographicHash::Sha256);
  HealthyModuleIdentity::LaunchBinding binding;
  binding.role = HealthyModuleIdentity::LaunchRole::Restore; binding.run = grant_.run;
  std::memcpy(binding.expectedGrantDigest.data(), digest_.constData(), size_t(digest_.size()));
  const auto state = state_;
  const auto module = [state] {
    auto hook = std::move(state->moduleHook); if (hook) hook();
    return state->module.load();
  };
  const auto endpoints = [state] {
    auto hook = std::move(state->endpointHook); if (hook) hook();
    return state->endpoints.load();
  };
  const std::shared_ptr<const std::atomic_bool> moduleState(state, &state->module);
  const std::shared_ptr<const std::atomic_bool> endpointState(state, &state->endpoints);
  const std::shared_ptr<const std::atomic<qint64>> clockState(state, &state->clockNs);
  return graph_->installRestoreBootstrapForTest(binding, 5, &tokens_->grant, &tokens_->control,
      &tokens_->ack, tokens_, module, endpoints, moduleState, endpointState, clockState);
}

bool HealthyRestoreTestFixture::installRenderer()
{
  const auto slot = prepared_->slot();
  const auto state = state_;
  const auto revoked = std::shared_ptr<const std::atomic_bool>(graph_->access_, &graph_->access_->revoked);
  if (!slot->bindModuleForTest([state] { return state->module.load(); }, revoked) ||
      !slot->beginCreation() || !slot->beforeCreate() || !slot->recordCreated(&tokens_->context)) return false;
  const auto retained = tokens_;
  HealthyRetirement::Operations ops;
  ops.currentIdentity = [] { return Identity{11, 12, 13}; };
  ops.unregisterCallback = [retained](void*) { Q_UNUSED(retained); };
  ops.invalidateQueued = [](std::uint64_t) { return true; };
  ops.freeContext = [](void*) {};
  ops.clearPointer = [](void*) { return true; };
  if (!slot->install({11, 12, 13}, &tokens_->context, ops)) return false;
  renderer_ = std::make_unique<HealthyRestoreRendererFixture>(slot);
  return renderer_->initialize();
}

bool HealthyRestoreTestFixture::acceptGrant()
{
  if (!graph_ || !graph_->session_) return false;
  QByteArray response;
  if (!graph_->restoreGrant(encodeGrant(grant_), true, response)) return false;
  const auto& session = *graph_->session_;
  const GrantAccepted expected{grant_.run, 5, grant_.grant, grant_.revision, digest_,
      session.host, session.core, session.intentRevision, session.attempt};
  GrantAccepted accepted;
  if (!decodeGrantAccepted(response, expected, accepted) || !accepted.host || !accepted.core ||
      !accepted.intentRevision || !accepted.attempt || accepted.host != session.host ||
      accepted.core != session.core || accepted.intentRevision != session.intentRevision ||
      accepted.attempt != session.attempt) return false;
  grantAccepted_ = response;
  return true;
}

bool HealthyRestoreTestFixture::installPermit()
{
  if (!graph_ || grantAccepted_.isEmpty()) return false;
  QByteArray response;
  return graph_->restoreControl(encodeRestorePermit({grant_.run, 5, grant_.grant, grant_.revision, 1, digest_}), response);
}

bool HealthyRestoreTestFixture::acceptGrantAndPermit() { return acceptGrant() && installPermit(); }

bool HealthyRestoreTestFixture::load() { return graph_ && graph_->restoreLoad(); }
bool HealthyRestoreTestFixture::seek() { return graph_ && graph_->restoreSeek(); }
PlayerComponent* HealthyRestoreTestFixture::player() const { return graph_ ? graph_->player_.get() : nullptr; }
std::shared_ptr<RetirementClaim> HealthyRestoreTestFixture::claim() const
{ return graph_ ? std::atomic_load(&graph_->access_->intentClaim) : nullptr; }
std::shared_ptr<ChildGate> HealthyRestoreTestFixture::gate() const
{ return graph_ && graph_->session_ ? graph_->session_->gate : nullptr; }
QString HealthyRestoreTestFixture::fixturePath() const { return media_ ? media_->fileName() : QString{}; }
