#include "PlayerIntentObservation.h"
#include <cstdlib>
#define CHECK(value) do { if (!(value)) std::abort(); } while (false)

using O = PlayerIntentObservation;

static void nestedAndTopLevel()
{
  CHECK(O::reset());
  {
    O::Scope load(O::Kind::LoadEnter);
    {
      O::Scope stop(O::Kind::StopEnter);
      O::stopResult(O::Outcome::Accepted);
    }
    O::queueDispatch();
  }
  { O::Scope stop(O::Kind::StopEnter); O::stopResult(O::Outcome::Accepted); }
  const auto s = O::snapshot();
  CHECK(s.healthy && s.count == 9);
  CHECK(s.events[0].kind == O::Kind::LoadEnter && s.events[0].parent == 0);
  CHECK(s.events[1].kind == O::Kind::StopEnter && s.events[1].parent == s.events[0].invocation);
  CHECK(s.events[2].kind == O::Kind::StopResult && s.events[2].invocation == s.events[1].invocation);
  CHECK(s.events[2].outcome == O::Outcome::Accepted);
  CHECK(s.events[4].kind == O::Kind::QueueDispatch && s.events[4].invocation == s.events[0].invocation);
  CHECK(s.events[6].kind == O::Kind::StopEnter && s.events[6].parent == 0);
}

static void statusAndBounds()
{
  for (auto outcome : {O::Outcome::GuardRejected, O::Outcome::Unavailable, O::Outcome::Error, O::Outcome::Accepted}) {
    CHECK(O::reset());
    { O::Scope stop(O::Kind::StopEnter); O::stopResult(outcome, -7); }
    const auto s = O::snapshot();
    CHECK(s.healthy && s.events[1].outcome == outcome && s.events[1].error == -7);
  }
  CHECK(O::reset());
  for (int i = 0; i < 100; ++i) { O::Scope stop(O::Kind::StopEnter); O::stopResult(O::Outcome::Accepted); }
  CHECK(!O::snapshot().healthy);
  CHECK(O::reset());
  { O::Scope load(O::Kind::LoadEnter); O::Scope nested(O::Kind::LoadEnter); }
  CHECK(!O::snapshot().healthy);
}

int main() { nestedAndTopLevel(); statusAndBounds(); }
