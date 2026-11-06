#pragma once
#ifdef _WIN32
#include "module_endpoints.h"
#include <QByteArray>
#include <QString>
#include <memory>
#include <functional>

namespace HealthyRetirement {
class PreparedGraph;

// Driven only by the existing GUI owner loop; this object owns no event loop.
class ChildSession {
public:
  enum class Progress { Pending, Ready, Failed };
  ChildSession(PreparedGraph& graph, std::shared_ptr<HealthyModuleIdentity::ChildEndpoints> endpoints,
               std::uint64_t generation);
#ifdef HEALTHY_CHILD_SESSION_TESTING
  struct TestOperations {
    std::function<bool()> valid;
    std::function<bool(const QString&)> initialize;
    std::function<bool(const QByteArray&, bool, QByteArray&)> grant;
    std::function<bool(const QByteArray&, QByteArray&)> control;
    std::function<bool()> load, seek, revoke;
    std::function<void()> invalidate;
  };
  ChildSession(std::shared_ptr<HealthyModuleIdentity::ChildEndpoints> endpoints,
               std::uint64_t generation, TestOperations operations);
#endif
  bool beginRestore(const QString& fixturePath);
  Progress pollRestore();
  bool load();
  bool seek();
  void invalidate();
private:
  using Io = FixtureRestart::IoProgress;
  using Role = HealthyModuleIdentity::EndpointRole;
  enum class GrantStage { Header, Body, Eof, Done };
  enum class ControlStage { Header, Body };
  bool valid() const;
  bool fail();
  Io beginRead(Role role, unsigned size);
  Io advance(Role role, Io& progress);
  bool pollGrant();
  bool pollControl();
  bool pollAck();
  bool queueAck(const QByteArray& frame, bool grant);
  bool initialize(const QString& path);
  bool grant(const QByteArray& frame, QByteArray& response);
  bool control(const QByteArray& frame, QByteArray& response);
  bool dispatch(bool seeking);
  void revokeGraph();
  PreparedGraph* graph_;
  std::shared_ptr<HealthyModuleIdentity::ChildEndpoints> endpoints_;
  const std::uint64_t generation_;
  GrantStage grantStage_ = GrantStage::Header;
  ControlStage controlStage_ = ControlStage::Header;
  Io grantIo_ = Io::Failed, controlIo_ = Io::Failed, ackIo_ = Io::Failed;
  QByteArray grantFrame_, controlFrame_, ackFrame_;
  bool started_ = false, failed_ = false, grantAck_ = false, ackIsGrant_ = false;
#ifdef HEALTHY_CHILD_SESSION_TESTING
  TestOperations test_;
  bool testing_ = false;
#endif
};
}
#endif
