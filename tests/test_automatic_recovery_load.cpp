#include "player/AutomaticRecoveryLoad.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #x); std::abort(); } } while (false)

using Load = AutomaticRecoveryLoad;
using State = Load::State;
static constexpr Load::Key key{1, 2, 3};

static void equalTimestampAllowsEitherEventOrder()
{
  for (int order = 0; order != 2; ++order) {
    Load load;
    CHECK(load.begin(key, 1, 1000));
    if (order == 0) {
      load.reply(load.userdata(), true, 7, 100);
      load.started(7, 100);
    } else {
      load.started(7, 100);
      load.reply(load.userdata(), true, 7, 100);
    }
    CHECK(load.poll(key, 100) == State::Accepted);
    CHECK(load.entry() == 7);
  }
}

static void backwardsEventsFailClosed()
{
  Load start;
  CHECK(start.begin(key, 1, 1000));
  start.reply(start.userdata(), true, 7, 100);
  start.started(7, 50);
  CHECK(start.state() == State::Failed);
  CHECK(start.entry() == 0);

  Load poll;
  CHECK(poll.begin(key, 1, 1000));
  poll.started(7, 100);
  CHECK(poll.poll(key, 50) == State::Failed);
  CHECK(poll.entry() == 0);
}

static void staleIdentityAndUserdataCannotAuthorize()
{
  Load load;
  CHECK(load.begin(key, 1, 1000));
  load.reply(load.userdata() + 1, true, 7, 100);
  load.started(7, 100);
  CHECK(load.poll(key, 100) == State::Pending);
  load.reply(load.userdata(), true, 7, 100);
  CHECK(load.poll(key, 100) == State::Accepted);
  for (auto current : {Load::Key{2, 2, 3}, Load::Key{1, 3, 3}, Load::Key{1, 2, 4}}) {
    Load other;
    CHECK(other.begin(key, 1, 1000));
    CHECK(other.poll(current, 100) == State::Failed);
    CHECK(other.entry() == 0);
  }
}

static void duplicateMismatchEndAndSequenceFailClosed()
{
  Load duplicate;
  CHECK(duplicate.begin(key, 1, 1000));
  duplicate.reply(duplicate.userdata(), true, 7, 100);
  duplicate.reply(duplicate.userdata(), true, 7, 100);
  CHECK(duplicate.state() == State::Failed);

  Load mismatch;
  CHECK(mismatch.begin(key, 1, 1000));
  mismatch.reply(mismatch.userdata(), true, 7, 100);
  mismatch.started(8, 100);
  CHECK(mismatch.state() == State::Failed);

  Load ended;
  CHECK(ended.begin(key, 1, 1000));
  ended.started(7, 100);
  ended.ended(7);
  CHECK(ended.state() == State::Failed);
  CHECK(!ended.begin(key, 1, 1000));
  CHECK(ended.begin(key, 2, 1000));
  CHECK(!ended.begin(key, 3, 1000));
}

static void exactDeadlineDoesNotAccept()
{
  Load reply;
  CHECK(reply.begin(key, 1, 100));
  reply.reply(reply.userdata(), true, 7, 100);
  CHECK(reply.state() == State::Expired);
  Load start;
  CHECK(start.begin(key, 1, 100));
  start.started(7, 100);
  CHECK(start.state() == State::Expired);
  Load poll;
  CHECK(poll.begin(key, 1, 100));
  CHECK(poll.poll(key, 100) == State::Expired);
}

int main()
{
  equalTimestampAllowsEitherEventOrder();
  backwardsEventsFailClosed();
  staleIdentityAndUserdataCannotAuthorize();
  duplicateMismatchEndAndSequenceFailClosed();
  exactDeadlineDoesNotAccept();
}
