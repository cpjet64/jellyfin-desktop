#pragma once

#ifndef HEALTHY_RETIREMENT_TESTING
#error "The restore fixture is restricted to the uniform test overlay"
#endif

#include "graph.h"
#include "fixture_restart/RestartBroker.h"
#include <QByteArray>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

class PlayerComponent;
class QTemporaryFile;
class HealthyRestoreRendererFixture;

// One active Graph per process; call initializeProcessCache before any Player.
// Publish operations issue actual renderer tickets; qualification remains in Player.
class HealthyRestoreTestFixture final {
public:
  struct State {
    std::atomic_bool module{true}, endpoints{true};
    std::atomic<qint64> clockNs{0}, clockOffset{0};
    unsigned clockCalls = 0, clockHookAt = 0;
    unsigned loads = 0, seeks = 0, stops = 0;
    quint64 idleQuery = 0, positionQuery = 0;
    bool pausedLoad = false;
    int loadResult = 0, seekResult = 0;
    quint64 lastUserdata = 0;
    std::vector<QByteArray> lastArguments;
    std::function<void()> moduleHook, endpointHook, clockHook, submissionHook;
  };

  HealthyRestoreTestFixture();
  ~HealthyRestoreTestFixture();
  HealthyRestoreTestFixture(const HealthyRestoreTestFixture&) = delete;
  HealthyRestoreTestFixture& operator=(const HealthyRestoreTestFixture&) = delete;

  static void initializeProcessCache();
  bool initialize(bool desiredPaused = false, qint64 checkpointMs = 3000);
  bool prepare(bool desiredPaused = false, qint64 checkpointMs = 3000);
  bool initializeRestore();
  bool cleanupBindPending() const;
  bool acceptGrant();
  bool installPermit();
  bool acceptGrantAndPermit();
  bool proveNoLoadCleanup();
  bool load();
  bool replyLoad(qint64 entry, bool startFirst = false, bool withStart = true, int error = 0);
  bool startFile(qint64 entry);
  bool observe(quint64 id, qint64 entry, const QByteArray& position);
  bool publishBaseline();
  bool armTarget();
  bool seek();
  void replySeekAndRestart(int error = 0, bool restartFirst = false, bool restart = true);
  bool publishTarget(int index = 90);
  bool replaceFramebuffer();
  bool cancel(quint64 sequence, quint64 revision, QByteArray& response);
  bool pollCancellation(QByteArray& response);

  HealthyRetirement::FixtureGraph* graph() const { return graph_; }
  PlayerComponent* player() const;
  std::shared_ptr<HealthyRetirement::RetirementClaim> claim() const;
  std::shared_ptr<FixtureRestart::ChildGate> gate() const;
  QString fixturePath() const;
  std::shared_ptr<State> state() const { return state_; }
  HealthyRetirement::PreparedGraph* prepared() const { return prepared_.get(); }
  const FixtureRestart::Grant& grant() const { return grant_; }
  const QByteArray& digest() const { return digest_; }
  const QByteArray& grantAccepted() const { return grantAccepted_; }

private:
  struct Tokens { int grant = 0, control = 0, ack = 0, context = 0; };
  static qint64 now();
  bool installRenderer();
  bool installBootstrap(bool desiredPaused, qint64 checkpointMs);
  std::shared_ptr<Tokens> tokens_;
  std::shared_ptr<State> state_;
  std::unique_ptr<HealthyRetirement::PreparedGraph> prepared_;
  HealthyRetirement::FixtureGraph* graph_ = nullptr;
  std::unique_ptr<QTemporaryFile> media_;
  std::unique_ptr<HealthyRestoreRendererFixture> renderer_;
  FixtureRestart::Grant grant_;
  QByteArray digest_;
  QByteArray grantAccepted_;
};
