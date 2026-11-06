#include "player/NativePlaybackObservation.h"
#include <QCoreApplication>
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "failure at %d\n", __LINE__); std::abort(); } } while (false)

static void validAndBounded()
{
  qint64 position = -1;
  CHECK(NativePlaybackObservation::decode("NR1|1|42|yes|yes|10.025|yes|no|no", 42, 30000, true, position));
  CHECK(position == 10025);
  CHECK(NativePlaybackObservation::decode("NR1|1|42|yes|yes|0|no|no|no", 42, 30000, false, position));
  CHECK(position == 0);
}

static void malformed()
{
  const QByteArray values[] = {
    "NR1|2|42|yes|yes|1|yes|no|no", "NR1|1|41|yes|yes|1|yes|no|no",
    "NR1|1|42|no|yes|1|yes|no|no", "NR1|1|42|yes|no|1|yes|no|no",
    "NR1|1|42|yes|yes|ERR|yes|no|no", "NR1|1|42|yes|yes|NaN|yes|no|no",
    "NR1|1|42|yes|yes|1e999|yes|no|no", "NR1|1|42|yes|yes|-1|yes|no|no",
    "NR1|1|42|yes|yes|31|yes|no|no", "NR1|1|42|yes|yes|1|no|no|no",
    "NR1|1|42|yes|yes|1|yes|yes|no", "NR1|1|42|yes|yes|1|yes|no|yes",
    "NR1|1|42|yes|yes|1|yes|no|no|extra", QByteArray(257, '1')};
  for (const auto& value : values) {
    qint64 position = 77;
    CHECK(!NativePlaybackObservation::decode(value, 42, 30000, true, position));
    CHECK(position == 77);
  }
}

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  validAndBounded();
  malformed();
}
