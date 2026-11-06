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
  HealthyModuleIdentity::LaunchBinding observed; std::uint64_t launch = 0;
  DISPATCH_CHECK(!graph->restoreLaunch(observed, launch));
  DISPATCH_CHECK(!graph->installRestoreBootstrapForTest(binding, 5, &tokens->grant, &tokens->grant,
                                                        &tokens->ack, tokens, yes, yes));
  DISPATCH_CHECK(!graph->installRestoreBootstrapForTest(binding, 5, nullptr, &tokens->control,
                                                        &tokens->ack, tokens, yes, yes));
  auto wrong = binding; wrong.role = HealthyModuleIdentity::LaunchRole::Source;
  DISPATCH_CHECK(!graph->installRestoreBootstrapForTest(wrong, 5, &tokens->grant, &tokens->control,
                                                        &tokens->ack, tokens, yes, yes));
  wrong = binding; wrong.expectedGrantDigest.fill(0);
  DISPATCH_CHECK(!graph->installRestoreBootstrapForTest(wrong, 5, &tokens->grant, &tokens->control,
                                                        &tokens->ack, tokens, yes, yes));
  std::weak_ptr<void> expired;
  DISPATCH_CHECK(!graph->installRestoreBootstrapForTest(binding, 5, &tokens->grant, &tokens->control,
                                                        &tokens->ack, expired, yes, yes));
  DISPATCH_CHECK(graph->installRestoreBootstrapForTest(binding, 5, &tokens->grant, &tokens->control,
                                                       &tokens->ack, tokens, yes, yes));
  DISPATCH_CHECK(!graph->installRestoreBootstrapForTest(binding, 5, &tokens->grant, &tokens->control,
                                                        &tokens->ack, tokens, yes, yes));
  DISPATCH_CHECK(graph->restoreLaunch(observed, launch) && launch == 5 && observed.run == 1);
  tokens.reset();
  DISPATCH_CHECK(!graph->restoreLaunch(observed, launch));
  DISPATCH_CHECK(!graph->restoreToken(HealthyModuleIdentity::EndpointRole::Control));
}
