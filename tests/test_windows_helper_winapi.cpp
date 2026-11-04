#include "windows_helper_winapi.h"

static DWORD observedError = 0;
static BOOL WINAPI failExit(HANDLE, LPDWORD value)
{ *value = 0; observedError = 101; return FALSE; }
static BOOL WINAPI failAccounting(HANDLE, JOBOBJECTINFOCLASS, LPVOID, DWORD, LPDWORD)
{ observedError = 102; return FALSE; }
static BOOL WINAPI failTermination(HANDLE, UINT)
{ observedError = 103; return FALSE; }
static DWORD WINAPI getError() { return observedError; }

int main()
{
  HelperWinApi api;
  api.exitCode = failExit;
  api.accounting = failAccounting;
  api.terminate = failTermination;
  api.lastError = getError;
  const HelperQuery exit = queryExit(api, nullptr);
  const HelperQuery active = queryActive(api, nullptr);
  const HelperQuery termination = terminateOwnedJob(api, nullptr);
  return (!exit.known && exit.value == 0 && exit.error == 101 &&
          !active.known && active.value == 0 && active.error == 102 &&
          !termination.known && termination.error == 103) ? 0 : 1;
}
