#include "windows_helper_outcome.h"

constexpr HelperObservation normal{true, true, 0, true, 0, false, false, true, 0};
constexpr HelperObservation nonzero{true, true, 42, true, 0, false, false, true, 0};
constexpr HelperObservation missingExit{true, false, 0, true, 0, false, false, true, 0};
constexpr HelperObservation missingCount{true, true, 0, false, 0, true, true, true, 0};
constexpr HelperObservation failedTermination{true, true, 0, true, 1, true, false, true, 0};
constexpr HelperObservation missingFinal{true, true, 0, true, 0, false, false, false, 0};
static_assert(successfulOutcome(normal));
static_assert(!successfulOutcome(nonzero) && !needsTermination(nonzero));
static_assert(!successfulOutcome(missingExit));
static_assert(!successfulOutcome(missingCount) && needsTermination(missingCount));
static_assert(!successfulOutcome(failedTermination) && needsTermination(failedTermination));
static_assert(!successfulOutcome(missingFinal));
int main() { return 0; }
