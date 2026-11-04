#pragma once
#include <windows.h>

struct HelperWinApi {
  decltype(&GetExitCodeProcess) exitCode = &GetExitCodeProcess;
  decltype(&QueryInformationJobObject) accounting = &QueryInformationJobObject;
  decltype(&TerminateJobObject) terminate = &TerminateJobObject;
  decltype(&GetLastError) lastError = &GetLastError;
};

struct HelperQuery { bool known; DWORD value; DWORD error; };

inline HelperQuery queryExit(const HelperWinApi& api, HANDLE process)
{
  DWORD value = 0;
  if (!api.exitCode(process, &value)) return {false, 0, api.lastError()};
  return {true, value, 0};
}

inline HelperQuery queryActive(const HelperWinApi& api, HANDLE job)
{
  JOBOBJECT_BASIC_ACCOUNTING_INFORMATION information{};
  if (!api.accounting(job, JobObjectBasicAccountingInformation, &information, sizeof(information), nullptr))
    return {false, 0, api.lastError()};
  return {true, information.ActiveProcesses, 0};
}

inline HelperQuery terminateOwnedJob(const HelperWinApi& api, HANDLE job)
{
  if (!api.terminate(job, 1)) return {false, 0, api.lastError()};
  return {true, 0, 0};
}
