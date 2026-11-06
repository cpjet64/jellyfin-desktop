#include "child_session.h"
#ifdef _WIN32
#include "graph.h"
#include "module_channel.h"
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
#ifdef HEALTHY_CHILD_SESSION_TESTING
  if (testing_) { if (test_.revoke) test_.revoke(); return; }
#endif
#ifndef HEALTHY_CHILD_SESSION_HEADLESS
  if (graph_) if (auto* graph = graph_->graphOnGuiThread()) graph->restoreRevoke();
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
