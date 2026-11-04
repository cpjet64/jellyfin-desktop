#pragma once

#include <QtTest>
#include <atomic>
#include <functional>

inline void finishErrorCase(std::atomic<bool>& fatal, const std::function<bool()>& cleanup)
{
  QVERIFY(cleanup());
  fatal.store(true);
  QFAIL("Playback terminated with an error; cleanup did not clear the failure");
}
