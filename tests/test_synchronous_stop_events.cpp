#include "synchronous_stop_events.h"
#include <cstdlib>

#define CHECK(value) do { if (!(value)) std::abort(); } while (false)

int main()
{
  using K = StopEventKind;
  using S = StopEventState;
  const std::vector<StopEvent> none{{K::Start, 10}, {K::End, 10}};
  CHECK(classifyStopEvents(none, 1, 10) == S::Settled); // B may never start.
  CHECK(classifyStopEvents({{K::Start, 10}, {K::End, 10}, {K::Start, 20}}, 1, 10) == S::Pending);
  CHECK(classifyStopEvents({{K::Start, 10}, {K::End, 10}, {K::Start, 20}, {K::End, 20}}, 1, 10) == S::Settled);
  CHECK(classifyStopEvents({{K::Start, 10}, {K::Start, 20}, {K::Start, 20}, {K::End, 20}}, 1, 10) == S::Invalid);
  CHECK(classifyStopEvents({{K::Start, 10}, {K::Start, 20}, {K::Start, 30},
                            {K::End, 20}, {K::End, 30}}, 1, 10) == S::Settled);
  CHECK(classifyStopEvents({{K::Start, 10}, {K::Start, 20}, {K::Start, 30},
                            {K::End, 20}}, 1, 10) == S::Pending);
  CHECK(classifyStopEvents({{K::Start, 10}, {K::Start, 20}, {K::Start, 30},
                            {K::End, 30}, {K::End, 20}}, 1, 10) == S::Settled);
  CHECK(classifyStopEvents({{K::Start, 10}, {K::End, 10}, {K::End, 10}}, 1, 10) == S::Invalid);
  CHECK(classifyStopEvents({{K::Start, 10}, {K::End, 10}, {K::End, 10}}, 2, 10) == S::Invalid);
  CHECK(classifyStopEvents({{K::Start, 10}, {K::End, 10}}, 2, 10) == S::Invalid);
  CHECK(classifyStopEvents({{K::Other, 0}, {K::End, 10}}, 1, 10) == S::Invalid);
  CHECK(classifyStopEvents({{K::Start, 10}, {K::Start, 10}}, 2, 10) == S::Invalid);
  CHECK(classifyStopEvents({{K::Start, 10}, {K::Start, 20}, {K::End, 21}}, 1, 10) == S::Invalid);
  CHECK(classifyStopEvents({{K::Start, 10}, {K::Start, 20}, {K::End, 20}, {K::End, 20}}, 1, 10) == S::Invalid);
  CHECK(classifyStopEvents({{K::Start, 10}, {K::Start, 10}}, 1, 10) == S::Invalid);
  CHECK(classifyStopEvents({{K::Start, 10}, {K::Overflow, 0}}, 1, 10) == S::Invalid);
  CHECK(classifyStopEvents(none, 3, 10) == S::Invalid);
  CHECK(classifyStopEvents(none, 1, 0) == S::Invalid);
  CHECK(classifyStopEvents(std::vector<StopEvent>(4097, {K::Other, 0}), 0, 10) == S::Invalid);
}
