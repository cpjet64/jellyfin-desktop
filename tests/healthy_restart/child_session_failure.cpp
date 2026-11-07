#include "child_session.h"
#ifdef _WIN32
#include "graph.h"
#include "../fixture_restart/RestartBroker.h"
#include <QScopedValueRollback>

namespace HealthyRetirement {
namespace { bool recordGateCancellation(void*) { return true; } }
void ChildSession::noteFailure(HealthyRestart::FailureCause cause)
{
  if (firstFailure_ == HealthyRestart::FailureCause::None) firstFailure_ = cause;
}

bool ChildSession::fail(HealthyRestart::FailureCause cause)
{
  noteFailure(cause);
  if (!failed_) {
    failed_ = true;
    if (restoreStarted_) {
      failurePhase_ = FailurePhase::Starting;
      if (restoreGate_ && !restoreGate_->canceled()) restoreGate_->cancel(recordGateCancellation, nullptr);
#ifndef HEALTHY_CHILD_SESSION_HEADLESS
      if (graph_) graph_->recordRestoreFailure(firstFailure_);
#endif
      if (endpoints_) endpoints_->beginContainment();
      bool begun = false;
      try { begun = beginFailureCleanup(); } catch (...) {}
      failurePhase_ = begun ? FailurePhase::Pending : FailurePhase::Unknown;
      return false;
    }
    revokeGraph();
#ifdef HEALTHY_CHILD_SESSION_TESTING
    if (testing_) { if (test_.invalidate) test_.invalidate(); return false; }
#endif
#ifndef HEALTHY_CHILD_SESSION_HEADLESS
    if (graph_) graph_->invalidate();
#endif
  }
  return false;
}

void ChildSession::invalidate() { fail(); }

bool ChildSession::beginFailureCleanup()
{
#ifdef HEALTHY_CHILD_SESSION_TESTING
  if (testing_) return test_.beginFailure && test_.beginFailure(firstFailure_);
#endif
#ifndef HEALTHY_CHILD_SESSION_HEADLESS
  return graph_ && graph_->beginRestoreFailure(firstFailure_);
#else
  return false;
#endif
}

ChildSession::Progress ChildSession::advanceFailureCleanup()
{
#ifdef HEALTHY_CHILD_SESSION_TESTING
  if (testing_) return test_.pollFailure ? test_.pollFailure() : Progress::Failed;
#endif
#ifndef HEALTHY_CHILD_SESSION_HEADLESS
  if (!graph_) return Progress::Failed;
  const auto result = graph_->pollRestoreFailure();
  return result == PreparedGraph::FailureProgress::Pending ? Progress::Pending :
      result == PreparedGraph::FailureProgress::Qualified ? Progress::Ready : Progress::Failed;
#else
  return Progress::Failed;
#endif
}

ChildSession::Progress ChildSession::pollFailureCleanup()
{
  if (failurePhase_ == FailurePhase::Starting || failurePolling_ || restorePolling_) return Progress::Pending;
  if (failurePhase_ == FailurePhase::Qualified) return Progress::Ready;
  if (failurePhase_ != FailurePhase::Pending) return Progress::Failed;
  QScopedValueRollback<bool> guard(failurePolling_, true);
  Progress result = Progress::Failed;
  try { result = advanceFailureCleanup(); } catch (...) {}
  if (result == Progress::Ready) failurePhase_ = FailurePhase::Qualified;
  else if (result == Progress::Failed) failurePhase_ = FailurePhase::Unknown;
  return result;
}

ChildSession::Progress ChildSession::settleAckWrite()
{
  if (!failed_) return Progress::Failed;
  if (ackFrame_.isEmpty()) return Progress::Ready;
  if (endpoints_->containmentUncertain(Role::Ack)) return Progress::Failed;
  if (failurePolling_ || restorePolling_) return Progress::Pending;
  QScopedValueRollback<bool> guard(restorePolling_, true);
  auto settlement = endpoints_->retainedSettlement(Role::Ack);
  if (!settlement) {
    if (ackIo_ == Io::Complete) return Progress::Failed;
    const auto result = endpoints_->poll(Role::Ack);
    settlement = endpoints_->retainedSettlement(Role::Ack);
    if (!settlement) return result == Io::Pending || result == Io::Unconfirmed ? Progress::Pending : Progress::Failed;
  }
  using Outcome = FixtureRestart::WindowsRestartIo::Settlement::Outcome;
  if (settlement->read || settlement->requested != unsigned(ackFrame_.size()) ||
      settlement->transferred > settlement->requested ||
      (settlement->outcome != Outcome::Completed && settlement->outcome != Outcome::Aborted) ||
      !endpoints_->consumeRetainedSettlement(Role::Ack, settlement->operation)) return Progress::Failed;
  ackFrame_.clear();
  return Progress::Ready;
}
}
#endif
