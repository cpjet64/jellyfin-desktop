#include "child_session.h"
#ifdef _WIN32
#include "graph.h"
#include "module_channel.h"
#include "module_gate.h"
#include "../fixture_restart/RestartWire.h"
#include <QtEndian>
#include <QScopedValueRollback>

namespace HealthyRetirement {
using FixtureRestart::IoProgress;
using HealthyModuleIdentity::EndpointRole;

#ifndef HEALTHY_CHILD_SESSION_HEADLESS
ChildSession::ChildSession(PreparedGraph& graph,
    std::shared_ptr<HealthyModuleIdentity::ChildEndpoints> endpoints, std::uint64_t generation)
  : graph_(&graph), endpoints_(std::move(endpoints)), generation_(generation) {}
#endif

#ifdef HEALTHY_CHILD_SESSION_TESTING
ChildSession::ChildSession(std::shared_ptr<HealthyModuleIdentity::ChildEndpoints> endpoints,
                           std::uint64_t generation, TestOperations operations)
  : graph_(nullptr), endpoints_(std::move(endpoints)), generation_(generation),
    test_(std::move(operations)), testing_(true) {}
#endif

bool ChildSession::valid() const
{
#ifdef HEALTHY_CHILD_SESSION_TESTING
  if (testing_) return !failed_ && endpoints_ && test_.valid && test_.valid();
#endif
#ifndef HEALTHY_CHILD_SESSION_HEADLESS
  const auto capability = endpoints_ ? endpoints_->capability() : nullptr;
  return !failed_ && graph_ && capability && capability->stillValid(generation_) &&
      graph_->ownsEndpoints(endpoints_) && graph_->graphOnGuiThread();
#else
  return false;
#endif
}

bool ChildSession::fail()
{
  if (!failed_) {
    revokeGraph();
    failed_ = true;
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

bool ChildSession::initialize(const QString& path)
{
#ifdef HEALTHY_CHILD_SESSION_TESTING
  if (testing_) return test_.initialize && test_.initialize(path);
#endif
#ifndef HEALTHY_CHILD_SESSION_HEADLESS
  return graph_->graphOnGuiThread()->restoreInitialize(path);
#else
  return false;
#endif
}

bool ChildSession::grant(const QByteArray& frame, QByteArray& response)
{
#ifdef HEALTHY_CHILD_SESSION_TESTING
  if (testing_) return test_.grant && test_.grant(frame, true, response);
#endif
#ifndef HEALTHY_CHILD_SESSION_HEADLESS
  return graph_->graphOnGuiThread()->restoreGrant(frame, true, response);
#else
  return false;
#endif
}

bool ChildSession::control(const QByteArray& frame, QByteArray& response)
{
#ifdef HEALTHY_CHILD_SESSION_TESTING
  if (testing_) return test_.control && test_.control(frame, response);
#endif
#ifndef HEALTHY_CHILD_SESSION_HEADLESS
  return graph_->graphOnGuiThread()->restoreControl(frame, response);
#else
  return false;
#endif
}

bool ChildSession::pollCancellation(QByteArray& response)
{
#ifdef HEALTHY_CHILD_SESSION_TESTING
  if (testing_) return test_.pollCancellation && test_.pollCancellation(response);
#endif
#ifndef HEALTHY_CHILD_SESSION_HEADLESS
  return graph_->graphOnGuiThread()->restorePollCancellation(response);
#else
  return false;
#endif
}

bool ChildSession::dispatch(bool seeking)
{
#ifdef HEALTHY_CHILD_SESSION_TESTING
  if (testing_) return seeking ? (test_.seek && test_.seek()) : (test_.load && test_.load());
#endif
#ifndef HEALTHY_CHILD_SESSION_HEADLESS
  auto* graph = graph_->graphOnGuiThread();
  return seeking ? graph->restoreSeek() : graph->restoreLoad();
#else
  return false;
#endif
}

void ChildSession::revokeGraph()
{
  if (sourceStage_ != SourceStage::Idle) return;
#ifdef HEALTHY_CHILD_SESSION_TESTING
  if (testing_) { if (test_.revoke) test_.revoke(); return; }
#endif
#ifndef HEALTHY_CHILD_SESSION_HEADLESS
  if (graph_) if (auto* graph = graph_->graphOnGuiThread()) graph->restoreRevoke();
#endif
}

bool ChildSession::beginSource(const QString& path, bool paused)
{
  if (started_ || !valid()) return fail();
  if (!sourceLoad(path, paused)) return fail();
  started_ = true;
  return true;
}

bool ChildSession::sourceLoad(const QString& path, bool paused)
{
#ifdef HEALTHY_CHILD_SESSION_TESTING
  if (testing_) return test_.sourceLoad && test_.sourceLoad(path, paused, sourceRun_, sourceLaunch_, sourceRevision_) &&
      sourceRun_ && sourceLaunch_ && sourceRevision_;
#endif
#ifndef HEALTHY_CHILD_SESSION_HEADLESS
  const auto capability = endpoints_->capability();
  HealthyModuleIdentity::LaunchBinding binding;
  if (!capability->launchBinding(generation_, binding) ||
      binding.role != HealthyModuleIdentity::LaunchRole::Source || !binding.run || !binding.revision ||
      !capability->launchId(generation_, sourceLaunch_) ||
      !graph_->graphOnGuiThread()->sourceLoad(path, paused)) return false;
  sourceRun_ = binding.run;
  sourceRevision_ = binding.revision;
  return true;
#else
  return false;
#endif
}

bool ChildSession::freezeSource()
{
  if (!started_ || sourceStage_ != SourceStage::Idle || !sourceRun_ || !valid()) return fail();
  QByteArray frame;
  if (!sourceFreeze(frame) || frame.isEmpty() || !valid()) return fail();
  sourceIo_ = endpoints_->begin(Role::Evidence, false, frame.constData(), unsigned(frame.size()));
  sourceStage_ = SourceStage::Evidence;
  return sourceIo_ == Io::Pending || sourceIo_ == Io::Complete || fail();
}

bool ChildSession::sourceControl()
{
  const auto result = advance(Role::Control, controlIo_);
  if (result == Io::Pending) return true;
  if (result != Io::Complete) return false;
  const auto* bytes = endpoints_->bytes(Role::Control);
  if (!bytes || bytes->size() != 24) return false;
  const QByteArray frame(reinterpret_cast<const char*>(bytes->data()), qsizetype(bytes->size()));
  FixtureRestart::Control command;
  if (!FixtureRestart::decodeControl(frame, command) || command.kind != FixtureRestart::ControlKind::InternalStop ||
      command.revision != sourceRevision_ || !sourceBeginStop(frame)) return false;
  stopSequence_ = command.sequence;
  controlIo_ = beginRead(Role::Control, 24);
  if (controlIo_ != Io::Pending && controlIo_ != Io::Complete) return false;
  sourceStage_ = SourceStage::Stop;
  return true;
}

bool ChildSession::retained() const
{
#ifdef HEALTHY_CHILD_SESSION_TESTING
  if (testing_) return valid();
#endif
#ifndef HEALTHY_CHILD_SESSION_HEADLESS
  const auto capability = endpoints_ ? endpoints_->capability() : nullptr;
  return !failed_ && graph_ && capability && capability->stillValid(generation_) && graph_->ownsEndpoints(endpoints_);
#else
  return false;
#endif
}

bool ChildSession::sourceFreeze(QByteArray& frame)
{
#ifdef HEALTHY_CHILD_SESSION_TESTING
  if (testing_) return test_.sourceFreeze && test_.sourceFreeze(frame);
#endif
#ifndef HEALTHY_CHILD_SESSION_HEADLESS
  return graph_->graphOnGuiThread()->sourceFreeze(frame);
#else
  return false;
#endif
}

bool ChildSession::sourceBeginStop(const QByteArray& frame)
{
#ifdef HEALTHY_CHILD_SESSION_TESTING
  if (testing_) return test_.sourceBeginStop && test_.sourceBeginStop(frame);
#endif
#ifndef HEALTHY_CHILD_SESSION_HEADLESS
  return graph_->graphOnGuiThread()->sourceBeginStop(frame);
#else
  return false;
#endif
}

ChildSession::Progress ChildSession::sourcePollStop(QByteArray& response)
{
#ifdef HEALTHY_CHILD_SESSION_TESTING
  if (testing_) return test_.sourcePollStop ? test_.sourcePollStop(response) : Progress::Failed;
#endif
#ifndef HEALTHY_CHILD_SESSION_HEADLESS
  const auto result = graph_->graphOnGuiThread()->sourcePollStop(response);
  return result == HealthyRestart::AsyncStop::Status::Pending ? Progress::Pending :
      result == HealthyRestart::AsyncStop::Status::Complete ? Progress::Ready : Progress::Failed;
#else
  return Progress::Failed;
#endif
}

bool ChildSession::sourceRetire()
{
#ifdef HEALTHY_CHILD_SESSION_TESTING
  if (testing_) return test_.sourceRetire && test_.sourceRetire();
#endif
#ifndef HEALTHY_CHILD_SESSION_HEADLESS
  return graph_->graphOnGuiThread()->requestRetirement();
#else
  return false;
#endif
}

ChildSession::Progress ChildSession::sourcePollRetire()
{
#ifdef HEALTHY_CHILD_SESSION_TESTING
  if (testing_) return test_.sourcePollRetire ? test_.sourcePollRetire() : Progress::Failed;
#endif
#ifndef HEALTHY_CHILD_SESSION_HEADLESS
  const auto result = graph_->graphOnGuiThread()->pollRetirement();
  return result == FixtureGraph::Progress::Pending ? Progress::Pending :
      result == FixtureGraph::Progress::Closed ? Progress::Ready : Progress::Failed;
#else
  return Progress::Failed;
#endif
}

bool ChildSession::sourceClosed(SourceClosed& result)
{
#ifdef HEALTHY_CHILD_SESSION_TESTING
  if (testing_) return test_.sourceClosed && test_.sourceClosed(result);
#endif
#ifndef HEALTHY_CHILD_SESSION_HEADLESS
  const auto closed = graph_->closedAck();
  if (!closed) return false;
  result = {closed->generation, closed->coreGeneration, closed->closureGeneration, closed->revision};
  return true;
#else
  return false;
#endif
}

ChildSession::Io ChildSession::beginRead(Role role, unsigned size)
{
  return endpoints_->begin(role, true, nullptr, size);
}

ChildSession::Io ChildSession::advance(Role role, Io& progress)
{
  if (progress == Io::Pending) progress = endpoints_->poll(role);
  return progress;
}

bool ChildSession::beginRestore(const QString& path)
{
  if (started_ || !valid() || !initialize(path)) return fail();
  started_ = true;
  restoreStarted_ = true;
  controlIo_ = beginRead(Role::Control, 4);
  if (!beginGrantRead(4)) return fail();
  return pollRestore() != Progress::Failed;
}

bool ChildSession::queueAck(const QByteArray& frame, bool grant)
{
  if (frame.isEmpty() || !ackFrame_.isEmpty()) return false;
  ackFrame_ = frame;
  ackIsGrant_ = grant;
  ackIo_ = endpoints_->begin(Role::Ack, false, frame.constData(), unsigned(frame.size()));
  return ackIo_ == Io::Pending || ackIo_ == Io::Complete;
}

bool ChildSession::pollAck()
{
  if (ackFrame_.isEmpty()) return true;
  const auto result = advance(Role::Ack, ackIo_);
  if (result == Io::Pending) return true;
  if (result != Io::Complete) return false;
  if (ackIsGrant_) grantAck_ = true;
  ackFrame_.clear();
  return true;
}


bool ChildSession::pollControl()
{
  if (!ackFrame_.isEmpty()) return true;
  const auto result = advance(Role::Control, controlIo_);
  if (result == Io::Pending) return true;
  if (result != Io::Complete) return false;
  const auto* bytes = endpoints_->bytes(Role::Control);
  if (!bytes) return false;
  const QByteArray part(reinterpret_cast<const char*>(bytes->data()), qsizetype(bytes->size()));
  if (controlStage_ == ControlStage::Header) {
    if (part != "JRC1" && part != "JRP1") return false;
    if (part == "JRP1" && !grantAck_) return false;
    controlFrame_ = part;
    controlStage_ = ControlStage::Body;
    controlIo_ = beginRead(Role::Control, part == "JRP1" ? 76 : 20);
    return controlIo_ == Io::Pending || controlIo_ == Io::Complete;
  }
  controlFrame_.append(part);
  QByteArray response;
  if (!control(controlFrame_, response)) return false;
  if (controlFrame_.left(4) == "JRC1") cancelReceived_ = true;
  if (!response.isEmpty() && cancelReceived_) cancelTerminal_ = true;
  if (!response.isEmpty() && !queueAck(response, false)) return false;
  controlStage_ = ControlStage::Header;
  controlIo_ = beginRead(Role::Control, 4);
  return controlIo_ == Io::Pending || controlIo_ == Io::Complete;
}

ChildSession::Progress ChildSession::pollRestore()
{
  if (restorePolling_) return Progress::Pending;
  QScopedValueRollback<bool> guard(restorePolling_, true);
  if (!started_ || !valid() || !pollAck() || !valid() || !pollControl() || !valid()) {
    fail(); return Progress::Failed;
  }
  if (!pollGrant()) { fail(); return Progress::Failed; }
  if (cancelReceived_ && !cancelTerminal_ && ackFrame_.isEmpty()) {
    QByteArray terminal;
    if (!pollCancellation(terminal)) { fail(); return Progress::Failed; }
    if (!terminal.isEmpty()) {
      cancelTerminal_ = true;
      if (!queueAck(terminal, false)) { fail(); return Progress::Failed; }
    }
  }
  if (!valid() || !pollAck() || !valid()) { fail(); return Progress::Failed; }
  if (cancelReceived_) return Progress::Pending;
  return grantAck_ ? Progress::Ready : Progress::Pending;
}


ChildSession::Progress ChildSession::pollSource()
{
  if (!started_ || !sourceRun_ || !retained()) { fail(); return Progress::Failed; }
  if (sourceStage_ == SourceStage::Done) return Progress::Ready;
  if (sourceStage_ == SourceStage::ControlCancel) {
    const auto result = controlCancelStarted_ ? endpoints_->poll(Role::Control) : endpoints_->cancel(Role::Control);
    controlCancelStarted_ = true;
    if (result == Io::Pending || result == Io::Unconfirmed) return Progress::Pending;
    if (result != Io::Failed || !endpoints_->canceledWithoutData(Role::Control) ||
        !queueAck(closedFrame_, false)) { fail(); return Progress::Failed; }
    sourceStage_ = SourceStage::ClosedAck;
    return Progress::Pending;
  }
  if (sourceStage_ == SourceStage::ClosedAck) {
    if (!pollAck()) { fail(); return Progress::Failed; }
    if (!ackFrame_.isEmpty()) return Progress::Pending;
    if (!endpoints_->closeSettled(Role::Ack)) { fail(); return Progress::Failed; }
    sourceStage_ = SourceStage::Done;
    return Progress::Ready;
  }
  if (!valid()) { fail(); return Progress::Failed; }
  if (sourceStage_ == SourceStage::Idle) return Progress::Pending;
  if (sourceStage_ == SourceStage::Evidence) {
    const auto result = advance(Role::Evidence, sourceIo_);
    if (result == Io::Pending) return Progress::Pending;
    if (result != Io::Complete || !endpoints_->closeSettled(Role::Evidence)) { fail(); return Progress::Failed; }
    controlIo_ = beginRead(Role::Control, 24);
    sourceStage_ = SourceStage::Control;
  }
  if (sourceStage_ == SourceStage::Control && !sourceControl()) { fail(); return Progress::Failed; }
  if (!valid()) { fail(); return Progress::Failed; }
  if ((sourceStage_ == SourceStage::Stop || sourceStage_ == SourceStage::StopAck ||
       sourceStage_ == SourceStage::Retirement) && advance(Role::Control, controlIo_) != Io::Pending) {
    fail(); return Progress::Failed;
  }
  if (sourceStage_ == SourceStage::Stop) {
    QByteArray response;
    const auto status = sourcePollStop(response);
    if (status == Progress::Failed || !valid()) { fail(); return Progress::Failed; }
    if (status == Progress::Pending) return Progress::Pending;
    if (!queueAck(response, false)) { fail(); return Progress::Failed; }
    sourceStage_ = SourceStage::StopAck;
  }
  if (sourceStage_ == SourceStage::StopAck) {
    if (!pollAck()) { fail(); return Progress::Failed; }
    if (!ackFrame_.isEmpty()) return Progress::Pending;
    if (!valid() || !sourceRetire()) { fail(); return Progress::Failed; }
    sourceStage_ = SourceStage::Retirement;
  }
  if (sourceStage_ == SourceStage::Retirement) {
    const auto status = sourcePollRetire();
    if (status == Progress::Failed) { fail(); return Progress::Failed; }
    if (status == Progress::Pending) return Progress::Pending;
    SourceClosed closed;
    if (!sourceClosed(closed) || closed.generation != generation_ || closed.revision != sourceRevision_ ||
        !retained()) { fail(); return Progress::Failed; }
    closedFrame_ = FixtureRestart::encodeClosed({sourceRun_, sourceLaunch_, stopSequence_, sourceRevision_,
        closed.core, closed.generation, closed.closure});
    if (closedFrame_.isEmpty()) { fail(); return Progress::Failed; }
    sourceStage_ = SourceStage::ControlCancel;
  }
  return Progress::Pending;
}

bool ChildSession::load()
{
  return pollRestore() == Progress::Ready && controlStage_ == ControlStage::Header &&
      ackFrame_.isEmpty() && controlIo_ == Io::Pending && valid() && dispatch(false);
}

bool ChildSession::seek()
{
  return pollRestore() == Progress::Ready && controlStage_ == ControlStage::Header &&
      ackFrame_.isEmpty() && controlIo_ == Io::Pending && valid() && dispatch(true);
}
}
#endif
