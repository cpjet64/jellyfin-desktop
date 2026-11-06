#include "test_healthy_graph_dispatch_fixture.h"

void TestHealthyGraphDispatch::bootstrapRejectsMalformed()
{
  const auto start = Fixture::now();
  auto prepared = FixtureGraph::prepare(9, start + 10000000000LL, start + 40000000000LL, Fixture::now);
  DISPATCH_CHECK(prepared);
  auto* graph = prepared->graphOnGuiThread(); DISPATCH_CHECK(graph);
  graph->containment_ = prepared->containment();
  auto tokens = std::make_shared<TokenStorage>();
  HealthyModuleIdentity::LaunchBinding binding;
  binding.role = HealthyModuleIdentity::LaunchRole::Restore; binding.run = 1;
  binding.expectedGrantDigest[0] = 1;
  const auto yes = [] { return true; };
  auto state = std::make_shared<Liveness>();
  state->clockNs = Fixture::now();
  const std::shared_ptr<const std::atomic_bool> moduleState(state, &state->module);
  const std::shared_ptr<const std::atomic_bool> endpointState(state, &state->endpoints);
  const std::shared_ptr<const std::atomic<qint64>> clockState(state, &state->clockNs);
  const auto install = [&](const HealthyModuleIdentity::LaunchBinding& value, const void* grant,
                           const void* control, const void* ack, std::weak_ptr<void> storage) {
    return graph->installRestoreBootstrapForTest(value, 5, grant, control, ack, std::move(storage),
                                                 yes, yes, moduleState, endpointState, clockState);
  };
  HealthyModuleIdentity::LaunchBinding observed; std::uint64_t launch = 0;
  DISPATCH_CHECK(!graph->restoreLaunch(observed, launch));
  DISPATCH_CHECK(!install(binding, &tokens->grant, &tokens->grant, &tokens->ack, tokens));
  DISPATCH_CHECK(!install(binding, nullptr, &tokens->control, &tokens->ack, tokens));
  auto wrong = binding; wrong.role = HealthyModuleIdentity::LaunchRole::Source;
  DISPATCH_CHECK(!install(wrong, &tokens->grant, &tokens->control, &tokens->ack, tokens));
  wrong = binding; wrong.expectedGrantDigest.fill(0);
  DISPATCH_CHECK(!install(wrong, &tokens->grant, &tokens->control, &tokens->ack, tokens));
  std::weak_ptr<void> expired;
  DISPATCH_CHECK(!install(binding, &tokens->grant, &tokens->control, &tokens->ack, expired));
  DISPATCH_CHECK(install(binding, &tokens->grant, &tokens->control, &tokens->ack, tokens));
  DISPATCH_CHECK(!install(binding, &tokens->grant, &tokens->control, &tokens->ack, tokens));
  DISPATCH_CHECK(graph->restoreLaunch(observed, launch) && launch == 5 && observed.run == 1);
  tokens.reset();
  DISPATCH_CHECK(!graph->restoreLaunch(observed, launch));
  DISPATCH_CHECK(!graph->restoreToken(HealthyModuleIdentity::EndpointRole::Control));
}
