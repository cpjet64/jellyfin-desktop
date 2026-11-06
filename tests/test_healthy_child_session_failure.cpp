#define main childRestoreTests
#include "test_healthy_child_session.cpp"
#undef main

namespace {
void initializationFailure()
{
  Script s; script = &s; auto e = endpoints(); CHECK(e);
  auto ops = operations(); int begins = 0, polls = 0; ChildSession* current = nullptr;
  ops.initialize = [](const QString&) { return false; };
  ops.beginFailure = [&](HealthyRestart::FailureCause cause) {
    CHECK(cause == HealthyRestart::FailureCause::InitializationFailure);
    ++begins; CHECK(current->pollFailureCleanup() == ChildSession::Progress::Pending); return true;
  };
  ops.pollFailure = [&] { return ++polls == 1 ? ChildSession::Progress::Pending : ChildSession::Progress::Ready; };
  ChildSession session(e, 1, ops); current = &session;
  CHECK(!session.beginRestore(QStringLiteral("fixture")) && begins == 1);
  CHECK(session.pollFailureCleanup() == ChildSession::Progress::Pending);
  CHECK(session.pollFailureCleanup() == ChildSession::Progress::Ready);
  CHECK(session.pollFailureCleanup() == ChildSession::Progress::Ready && polls == 2);
  CHECK(s.invalidations == 0 && s.writes == 0);
}
void reentrantGrantFailure()
{
  Script s; script = &s; auto e = endpoints(); CHECK(e);
  auto ops = operations(); ChildSession* current = nullptr; int begins = 0, polls = 0;
  ops.beginFailure = [&](HealthyRestart::FailureCause) { ++begins; return true; };
  ops.pollFailure = [&] { ++polls; return ChildSession::Progress::Ready; };
  ops.grant = [&](const QByteArray&, bool, QByteArray& ack) {
    current->invalidate(); CHECK(current->pollFailureCleanup() == ChildSession::Progress::Pending);
    ack = "JGA2-ack"; return true;
  };
  feed(101, grant()); s.eof[handle(101)] = true;
  ChildSession session(e, 1, ops); current = &session;
  CHECK(session.beginRestore(QStringLiteral("fixture")));
  for (int i = 0; i < 8 && session.pollRestore() != ChildSession::Progress::Failed; ++i) {}
  CHECK(session.pollRestore() == ChildSession::Progress::Failed && begins == 1);
  CHECK(s.writes == 0 && s.invalidations == 0 && polls == 0);
  CHECK(session.pollFailureCleanup() == ChildSession::Progress::Ready && polls == 1);
}
void unknownAndReentrantPoll()
{
  Script s; script = &s; auto e = endpoints(); CHECK(e);
  auto ops = operations(); ChildSession* current = nullptr; int polls = 0;
  ops.initialize = [](const QString&) { return false; };
  ops.beginFailure = [](HealthyRestart::FailureCause cause) { return cause == HealthyRestart::FailureCause::InitializationFailure; };
  ops.pollFailure = [&] {
    ++polls; CHECK(current->pollFailureCleanup() == ChildSession::Progress::Pending);
    CHECK(current->settleGrantRead() == ChildSession::Progress::Pending);
    return ChildSession::Progress::Failed;
  };
  ChildSession session(e, 1, ops); current = &session;
  CHECK(!session.beginRestore(QStringLiteral("fixture")));
  CHECK(session.pollFailureCleanup() == ChildSession::Progress::Failed);
  CHECK(session.pollFailureCleanup() == ChildSession::Progress::Failed && polls == 1);
  CHECK(s.invalidations == 0 && s.writes == 0);
}
void reentrantValidationFailure()
{
  Script s; script = &s; auto e = endpoints(); CHECK(e);
  auto ops = operations(); ChildSession* current = nullptr; int begins = 0;
  ops.beginFailure = [&](HealthyRestart::FailureCause) { ++begins; return true; };
  ops.validateCanceledGrant = [&](const QByteArray&) { current->invalidate(); return true; };
  feed(101, grant()); s.eof[handle(101)] = true;
  ChildSession session(e, 1, ops); current = &session;
  CHECK(session.beginRestore(QStringLiteral("fixture")));
  for (int i = 0; i < 8 && session.pollRestore() != ChildSession::Progress::Failed; ++i) {}
  CHECK(session.pollRestore() == ChildSession::Progress::Failed);
  CHECK(begins == 1 && s.grants == 0 && s.writes == 0 && s.invalidations == 0);
}
void protocolCauseSurvivesCleanupFailure()
{
  Script s; script = &s; auto e = endpoints(); CHECK(e); auto ops = operations(); int begins = 0;
  ops.beginFailure = [&](HealthyRestart::FailureCause cause) {
    ++begins; CHECK(cause == HealthyRestart::FailureCause::ProtocolViolation); return true;
  };
  ops.pollFailure = [] { return ChildSession::Progress::Failed; };
  feed(101, QByteArray::fromHex("00002400")); ChildSession session(e, 1, ops);
  CHECK(!session.beginRestore(QStringLiteral("fixture")));
  CHECK(session.pollFailureCleanup() == ChildSession::Progress::Failed);
  session.invalidate(); CHECK(begins == 1 && s.invalidations == 0);
}
void settleSubmittedAckAfterFailure()
{
  Script s; script = &s; s.holdAck = true; auto e = endpoints(); CHECK(e); auto ops = operations();
  feed(101, grant()); s.eof[handle(101)] = true;
  ChildSession session(e, 1, ops); CHECK(session.beginRestore(QStringLiteral("fixture")));
  for (int i = 0; i < 8; ++i) CHECK(session.pollRestore() == ChildSession::Progress::Pending);
  CHECK(s.writes == 1); session.invalidate();
  CHECK(session.settleAckWrite() == ChildSession::Progress::Pending);
  s.holdAck = false;
  CHECK(session.settleAckWrite() == ChildSession::Progress::Ready);
  CHECK(session.settleAckWrite() == ChildSession::Progress::Ready && s.writes == 1);
}
void settleControlWithoutDispatch()
{
  Script s; script = &s; auto e = endpoints(); CHECK(e); auto ops = operations();
  ChildSession session(e, 1, ops); CHECK(session.beginRestore(QStringLiteral("fixture")));
  const auto frame = cancel(); feed(102, frame.left(4));
  CHECK(session.pollRestore() == ChildSession::Progress::Pending);
  session.invalidate(); feed(102, frame.mid(4));
  CHECK(session.settleControlRead() == ChildSession::Progress::Ready);
  CHECK(s.controls == 0 && s.writes == 0 && s.invalidations == 0);
}
}
int main()
{
  initializationFailure(); reentrantGrantFailure(); unknownAndReentrantPoll();
  reentrantValidationFailure(); protocolCauseSurvivesCleanupFailure(); settleSubmittedAckAfterFailure();
  settleControlWithoutDispatch();
}
