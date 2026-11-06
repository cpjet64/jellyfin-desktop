#pragma once
#include "graph.h"
#include "fixture_restart/RestartBroker.h"
#include <memory>

class FixtureRestartAdapter;
namespace FixtureRestart { class RestartSourcePlayer; class RestartChildPlayer; }

namespace HealthyRetirement {
class FixtureGraph::Session {
public:
  Session();
  ~Session();
  std::shared_ptr<FixtureRestart::ChildGate> gate;
  std::unique_ptr<FixtureRestartAdapter> adapter;
  std::unique_ptr<FixtureRestart::RestartSourcePlayer> source;
  std::unique_ptr<FixtureRestart::RestartChildPlayer> child;
  FixtureRestart::Grant binding;
  quint64 host = 0, core = 0, intentRevision = 0, attempt = 0;
  quint64 run = 0, launch = 0, cancelSequence = 0, cancelRevision = 0;
  QByteArray digest;
};
}
