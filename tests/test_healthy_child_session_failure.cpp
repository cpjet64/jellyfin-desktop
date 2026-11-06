#define main childRestoreTests
#include "test_healthy_child_session.cpp"
#undef main

namespace {
void initializationFailure()
{
  Script s; script = &s; auto e = endpoints(); CHECK(e);
  auto ops = operations(); int begins = 0, polls = 0; ChildSession* current = nullptr;
  ops.initialize = [](const QString&) { return false; };
  ops.beginFailure = [&] {
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
  ops.beginFailure = [&] { ++begins; return true; };
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
  ops.beginFailure = [] { return true; };
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
}
int main() { initializationFailure(); reentrantGrantFailure(); unknownAndReentrantPoll(); }
