#include "test_healthy_graph_dispatch_fixture.h"
#include <QDir>

const char* dispatchCase = "setup";

std::int64_t TestHealthyGraphDispatch::Fixture::now()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

void TestHealthyGraphDispatch::Fixture::recordCallback(const std::shared_ptr<Liveness>& state, char kind)
{
  if (!state->trace) return;
  state->callbacks.push_back(kind);
  if (state->triggerAt && state->callbacks.size() == state->triggerAt) {
    state->finalCallbackFired = true;
    auto hook = std::move(state->finalCallbackHook); if (hook) hook();
  }
}

void TestHealthyGraphDispatch::Fixture::installRenderer()
{
  const auto slot = prepared->slot();
  const auto state = live;
  const std::shared_ptr<const std::atomic_bool> revoked(graph->access_, &graph->access_->revoked);
  DISPATCH_CHECK(slot->bindModuleForTest([state] { return state->module.load(); }, revoked));
  DISPATCH_CHECK(slot->beginCreation() && slot->beforeCreate() && slot->recordCreated(&tokens->rendererContext));
  HealthyRetirement::Operations ops;
  ops.currentIdentity = [] { return Identity{11, 12, 13}; };
  ops.unregisterCallback = [](void*) {};
  ops.invalidateQueued = [](std::uint64_t) { return true; };
  ops.freeContext = [](void*) {};
  ops.clearPointer = [](void*) { return true; };
  DISPATCH_CHECK(slot->install({11, 12, 13}, &tokens->rendererContext, ops));
  renderer = std::make_unique<TestCaptureCapability>(slot);
  DISPATCH_CHECK(renderer->initialize());
}

void TestHealthyGraphDispatch::Fixture::initialize(bool desiredPaused)
{
  fixture_restart::disarmCapture();
  const auto start = now();
  DISPATCH_CHECK(start >= 0 && start <= std::numeric_limits<std::int64_t>::max() - 40000000000LL);
  auto state = live;
  const auto clock = [state] {
    recordCallback(state, 'C');
    auto hook = std::move(state->clockHook); if (hook) hook();
    const auto current = now(), offset = state->clockOffset.load();
    DISPATCH_CHECK((offset <= 0 || current <= std::numeric_limits<std::int64_t>::max() - offset) &&
                   (offset >= 0 || current >= std::numeric_limits<std::int64_t>::min() - offset));
    const auto sampled = static_cast<qint64>(current + offset);
    state->clockNs.store(sampled);
    return sampled;
  };
  prepared = FixtureGraph::prepare(9, start + 10000000000LL, start + 40000000000LL, clock);
  DISPATCH_CHECK(prepared); graph = prepared->graphOnGuiThread(); DISPATCH_CHECK(graph);
  graph->containment_ = prepared->containment();
  graph->player_ = std::make_unique<PlayerComponent>();
  graph->player_->m_mpvGeneration = 8;
  graph->player_->m_restoreDispatch = [state](quint64, const char** args) {
    if (state->trace) state->callbacksAtSubmission = state->callbacks;
    if (!std::strcmp(args[0], "loadfile")) {
      ++state->loads;
      for (int i = 1; args[i]; ++i) if (!std::strcmp(args[i], "pause=yes")) state->pausedLoad = true;
    } else if (!std::strcmp(args[0], "seek")) ++state->seeks;
    auto hook = std::move(state->submissionHook); if (hook) hook();
    return 0;
  };
  graph->player_->m_observationDispatch = [](quint64, const char**) { return 0; };
  graph->player_->m_healthyStopClock = clock;
  media = std::make_unique<QTemporaryFile>(ownedFixtureTemplate());
  DISPATCH_CHECK(media->open());
  const QByteArray content("bounded headless fixture");
  DISPATCH_CHECK(media->write(content) == content.size() && media->flush());
  const auto asset = QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex();
  grant = {1, 3, 4, asset, 3000, desiredPaused};
  digest = QCryptographicHash::hash(encodeGrant(grant), QCryptographicHash::Sha256);
  HealthyModuleIdentity::LaunchBinding binding;
  binding.role = HealthyModuleIdentity::LaunchRole::Restore; binding.run = grant.run;
  std::memcpy(binding.expectedGrantDigest.data(), digest.constData(), size_t(digest.size()));
  auto module = [state] {
    recordCallback(state, 'M');
    auto hook = std::move(state->moduleHook); if (hook) hook();
    return state->module.load();
  };
  auto endpoints = [state] {
    recordCallback(state, 'E');
    auto hook = std::move(state->endpointHook); if (hook) hook();
    return state->endpoints.load();
  };
  const std::shared_ptr<const std::atomic_bool> moduleState(state, &state->module);
  const std::shared_ptr<const std::atomic_bool> endpointState(state, &state->endpoints);
  const std::shared_ptr<const std::atomic<qint64>> clockState(state, &state->clockNs);
  DISPATCH_CHECK(graph->installRestoreBootstrapForTest(binding, 5, &tokens->grant, &tokens->control,
      &tokens->ack, tokens, module, endpoints, moduleState, endpointState, clockState));
  installRenderer();
  DISPATCH_CHECK(graph->restoreInitialize(media->fileName()));
}

void TestHealthyGraphDispatch::Fixture::acceptGrantAndPermit()
{
  QByteArray response;
  DISPATCH_CHECK(graph->restoreGrant(encodeGrant(grant), true, response));
  const auto& session = *graph->session_;
  const GrantAccepted expected{grant.run, 5, grant.grant, grant.revision, digest,
      session.host, session.core, session.intentRevision, session.attempt};
  GrantAccepted accepted;
  DISPATCH_CHECK(decodeGrantAccepted(response, expected, accepted));
  DISPATCH_CHECK(accepted.host && accepted.core && accepted.intentRevision && accepted.attempt);
  response.clear();
  const RestorePermit permit{grant.run, 5, grant.grant, grant.revision, 1, digest};
  DISPATCH_CHECK(graph->restoreControl(encodeRestorePermit(permit), response));
}

void TestHealthyGraphDispatch::Fixture::replyLoad(qint64 entry, bool startFirst, bool withStart)
{
  auto& player = *graph->player_;
  mpv_node value{}; value.format = MPV_FORMAT_INT64; value.u.int64 = entry;
  char field[] = "playlist_entry_id"; char* fields[] = {field};
  mpv_node_list list{}; list.num = 1; list.keys = fields; list.values = &value;
  mpv_event_command command{}; command.result.format = MPV_FORMAT_NODE_MAP; command.result.u.list = &list;
  mpv_event reply{}; reply.event_id = MPV_EVENT_COMMAND_REPLY;
  reply.reply_userdata = (quint64{1} << 63) | graph->session_->attempt; reply.data = &command;
  mpv_event_start_file start{}; start.playlist_entry_id = entry;
  mpv_event started{}; started.event_id = MPV_EVENT_START_FILE; started.data = &start;
  if (withStart && startFirst) player.observeNativeReceipt(&started);
  player.observeNativeReceipt(&reply);
  if (withStart && !startFirst) player.observeNativeReceipt(&started);
}

void TestHealthyGraphDispatch::Fixture::observe(quint64 id, qint64 entry, const QByteArray& position)
{
  auto& player = *graph->player_;
  if (!player.m_nativeObservation.pending)
    DISPATCH_CHECK(player.requestNativeObservation(player.m_nativeReceipts.snapshot().key, id, 10000, true));
  DISPATCH_CHECK(player.m_nativeObservation.pending && player.m_nativeObservation.id == id);
  QByteArray text = "NR1|1|" + QByteArray::number(entry) + "|yes|yes|" + position + "|yes|no|no";
  mpv_event_command command{}; command.result.format = MPV_FORMAT_STRING; command.result.u.string = text.data();
  mpv_event event{}; event.event_id = MPV_EVENT_COMMAND_REPLY;
  event.reply_userdata = (quint64{1} << 62) | id; event.data = &command;
  player.observeNativeReceipt(&event);
}

void TestHealthyGraphDispatch::Fixture::qualifyBaseline()
{
  DISPATCH_CHECK(graph->captureBaseline());
  DISPATCH_CHECK(renderer->queue()); renderer->publish(0);
  DISPATCH_CHECK(graph->captureTarget());
}

void TestHealthyGraphDispatch::Fixture::armTarget()
{
  DISPATCH_CHECK(!graph->captureTargetReady());
  DISPATCH_CHECK(renderer->queue() && graph->captureTargetReady());
}

void TestHealthyGraphDispatch::Fixture::completeSeekReplyAndRestart()
{
  mpv_event event{}; event.event_id = MPV_EVENT_COMMAND_REPLY;
  event.reply_userdata = (quint64{1} << 61) | graph->session_->attempt;
  graph->player_->observeNativeReceipt(&event);
  event = {}; event.event_id = MPV_EVENT_PLAYBACK_RESTART;
  graph->player_->observeNativeReceipt(&event);
}

bool TestHealthyGraphDispatch::Fixture::drivePausedHold()
{
  using Progress = FixtureGraph::PlaybackProgress;
  for (int count = 0; count != 32; ++count) {
    const auto progress = graph->pollRestorePlayback();
    if (progress == Progress::Ready) return true;
    if (progress == Progress::Failed) return false;
    auto& observation = graph->player_->m_nativeObservation;
    if (observation.pending) observe(observation.id, 42, "3.000");
    QThread::msleep(120);
  }
  return false;
}

void TestHealthyGraphDispatch::Fixture::checkNoUnpause() const
{
  DISPATCH_CHECK(!graph->player_->m_restoreUnpause.attempted);
}
