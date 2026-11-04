#pragma once

struct HelperObservation {
  bool waitSignaled = false;
  bool exitKnown = false;
  unsigned long exitCode = 0;
  bool activeBeforeKnown = false;
  unsigned long activeBefore = 0;
  bool terminationRequested = false;
  bool terminationSucceeded = false;
  bool activeAfterKnown = false;
  unsigned long activeAfter = 0;
};

constexpr bool needsTermination(const HelperObservation& value)
{
  return !value.activeBeforeKnown || value.activeBefore != 0;
}

constexpr bool successfulOutcome(const HelperObservation& value)
{
  return value.waitSignaled && value.exitKnown && value.exitCode == 0 &&
      value.activeBeforeKnown && value.activeBefore == 0 && !value.terminationRequested &&
      value.activeAfterKnown && value.activeAfter == 0;
}
