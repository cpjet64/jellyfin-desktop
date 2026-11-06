#include "child_session.h"
#ifdef _WIN32
#include "graph.h"
#include "module_channel.h"
#include "module_gate.h"
#include "../fixture_restart/RestartWire.h"
#include <QtEndian>

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
#ifdef HEALTHY_CHILD_SESSION_HEADLESS
  Q_UNUSED(path); Q_UNUSED(paused); return false;
#else
  if (started_ || !valid()) return fail();
  const auto capability = endpoints_->capability();
  HealthyModuleIdentity::LaunchBinding binding;
  if (!capability->launchBinding(generation_, binding) ||
      binding.role != HealthyModuleIdentity::LaunchRole::Source || !binding.run || !binding.revision ||
      !capability->launchId(generation_, sourceLaunch_) ||
      !graph_->graphOnGuiThread()->sourceLoad(path, paused)) return fail();
  sourceRun_ = binding.run;
  sourceRevision_ = binding.revision;
  started_ = true;
  return true;
#endif
}

bool ChildSession::freezeSource()
{
#ifdef HEALTHY_CHILD_SESSION_HEADLESS
  return false;
#else
  if (!started_ || sourceStage_ != SourceStage::Idle || !sourceRun_ || !valid()) return fail();
  QByteArray frame;
  if (!graph_->graphOnGuiThread()->sourceFreeze(frame) || frame.isEmpty() || !valid()) return fail();
  sourceIo_ = endpoints_->begin(Role::Evidence, false, frame.constData(), unsigned(frame.size()));
  sourceStage_ = SourceStage::Evidence;
  return sourceIo_ == Io::Pending || sourceIo_ == Io::Complete || fail();
#endif
}

bool ChildSession::sourceControl()
{
#ifdef HEALTHY_CHILD_SESSION_HEADLESS
  return false;
#else
  const auto result = advance(Role::Control, controlIo_);
  if (result == Io::Pending) return true;
  if (result != Io::Complete) return false;
  const auto* bytes = endpoints_->bytes(Role::Control);
  if (!bytes || bytes->size() != 24) return false;
  const QByteArray frame(reinterpret_cast<const char*>(bytes->data()), qsizetype(bytes->size()));
  FixtureRestart::Control command;
  if (!FixtureRestart::decodeControl(frame, command) || command.kind != FixtureRestart::ControlKind::InternalStop ||
      command.revision != sourceRevision_ || !graph_->graphOnGuiThread()->sourceBeginStop(frame)) return false;
  stopSequence_ = command.sequence;
  controlIo_ = beginRead(Role::Control, 24);
  if (controlIo_ != Io::Pending && controlIo_ != Io::Complete) return false;
  sourceStage_ = SourceStage::Stop;
  return true;
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
  controlIo_ = beginRead(Role::Control, 4);
  grantIo_ = beginRead(Role::Grant, 4);
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

bool ChildSession::pollGrant()
{
  if (grantStage_ == GrantStage::Done) return true;
  const auto result = advance(Role::Grant, grantIo_);
  if (result == Io::Pending) return true;
  if (grantStage_ == GrantStage::Eof) {
    if (result != Io::Eof) return false;
    if (!ackFrame_.isEmpty()) return true;
    QByteArray response;
    if (!grant(grantFrame_, response) || !queueAck(response, true)) return false;
    grantStage_ = GrantStage::Done;
    return true;
  }
  if (result != Io::Complete) return false;
  const auto* bytes = endpoints_->bytes(Role::Grant);
  if (!bytes) return false;
  const QByteArray part(reinterpret_cast<const char*>(bytes->data()), qsizetype(bytes->size()));
  if (grantStage_ == GrantStage::Header) {
    if (part.size() != 4) return false;
    const auto size = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(part.constData()));
    if (!size || size > 9212) return false;
    grantFrame_ = part;
    grantStage_ = GrantStage::Body;
    grantIo_ = beginRead(Role::Grant, size);
  } else {
    grantFrame_.append(part);
    grantStage_ = GrantStage::Eof;
    grantIo_ = beginRead(Role::Grant, 1);
  }
  return grantIo_ == Io::Pending || grantIo_ == Io::Complete || grantIo_ == Io::Eof;
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
  if (!response.isEmpty() && !queueAck(response, false)) return false;
  controlStage_ = ControlStage::Header;
  controlIo_ = beginRead(Role::Control, 4);
  return controlIo_ == Io::Pending || controlIo_ == Io::Complete;
}

ChildSession::Progress ChildSession::pollRestore()
{
  if (!started_ || !valid() || !pollAck() || !valid() || !pollControl() || !valid() ||
      !pollGrant() || !valid() || !pollAck() || !valid()) {
    fail(); return Progress::Failed;
  }
  return grantAck_ ? Progress::Ready : Progress::Pending;
}

ChildSession::Progress ChildSession::pollSource()
{
#ifdef HEALTHY_CHILD_SESSION_HEADLESS
  return Progress::Failed;
#else
  const auto capability = endpoints_ ? endpoints_->capability() : nullptr;
  const bool retained = !failed_ && graph_ && capability && capability->stillValid(generation_) &&
      graph_->ownsEndpoints(endpoints_);
  if (!started_ || !sourceRun_ || !retained) { fail(); return Progress::Failed; }
  if (sourceStage_ == SourceStage::Done) return Progress::Ready;
  if (sourceStage_ == SourceStage::ControlCancel) {
    const auto result = endpoints_->cancel(Role::Control);
    if (result == Io::Pending) return Progress::Pending;
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
  auto* graph = graph_->graphOnGuiThread();
  if (!graph) { fail(); return Progress::Failed; }
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
    const auto status = graph->sourcePollStop(response);
    if (status == HealthyRestart::AsyncStop::Status::Failed || !valid()) { fail(); return Progress::Failed; }
    if (status == HealthyRestart::AsyncStop::Status::Pending) return Progress::Pending;
    if (!queueAck(response, false)) { fail(); return Progress::Failed; }
    sourceStage_ = SourceStage::StopAck;
  }
  if (sourceStage_ == SourceStage::StopAck) {
    if (!pollAck()) { fail(); return Progress::Failed; }
    if (!ackFrame_.isEmpty()) return Progress::Pending;
    if (!valid() || !graph->requestRetirement()) { fail(); return Progress::Failed; }
    sourceStage_ = SourceStage::Retirement;
  }
  if (sourceStage_ == SourceStage::Retirement) {
    const auto status = graph->pollRetirement();
    if (status == FixtureGraph::Progress::Failed) { fail(); return Progress::Failed; }
    if (status == FixtureGraph::Progress::Pending) return Progress::Pending;
    const auto closed = graph_->closedAck();
    if (!closed || closed->generation != generation_ || closed->revision != sourceRevision_ ||
        !capability->stillValid(generation_) || !graph_->ownsEndpoints(endpoints_)) { fail(); return Progress::Failed; }
    closedFrame_ = FixtureRestart::encodeClosed({sourceRun_, sourceLaunch_, stopSequence_, sourceRevision_,
        closed->coreGeneration, closed->generation, closed->closureGeneration});
    if (closedFrame_.isEmpty()) { fail(); return Progress::Failed; }
    sourceStage_ = SourceStage::ControlCancel;
  }
  return Progress::Pending;
#endif
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
