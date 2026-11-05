extern "C" __declspec(dllimport) int mpv_fixture();
#include <cstdio>
#include <windows.h>
int main()
{
  const DWORD processBefore = GetErrorMode();
  DWORD previous = 0;
  if (!SetThreadErrorMode(SEM_NOGPFAULTERRORBOX, &previous)) return 125;
  const DWORD before = GetThreadErrorMode();
  std::fprintf(stderr, "fixture before thread=%lu process=%lu\n", before, processBefore);
  std::fflush(stderr);
  int value = mpv_fixture();
  const DWORD after = GetThreadErrorMode();
  const DWORD processAfter = GetErrorMode();
  const bool restored = SetThreadErrorMode(previous, nullptr) != FALSE;
  std::fprintf(stderr, "fixture after thread=%lu process=%lu\n", after, processAfter);
  std::fprintf(stderr, "before fixture return=%d\n", value);
  std::fflush(stderr);
  return before == after && processBefore == processAfter && restored ? value : 125;
}
