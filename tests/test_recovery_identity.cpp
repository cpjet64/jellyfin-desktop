#include "player/RecoveryIdentity.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #x); std::abort(); } } while (false)

int main()
{
  RecoveryIdentity::Counter bounded(2, 3);
  CHECK(bounded.take() == 2);
  CHECK(bounded.take() == 3);
  CHECK(bounded.take() == 0);
  CHECK(bounded.take() == 0);
  RecoveryIdentity::Counter exhausted(0, 3);
  CHECK(exhausted.take() == 0);
  CHECK(exhausted.take() == 0);
  const auto first = RecoveryIdentity::take(RecoveryIdentity::Domain::FixtureLoad);
  const auto second = RecoveryIdentity::take(RecoveryIdentity::Domain::FixtureLoad);
  CHECK(first != 0 && second != 0 && first != second);
}
