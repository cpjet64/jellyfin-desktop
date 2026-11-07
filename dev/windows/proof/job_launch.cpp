#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <string>
#include <vector>

static unsigned ownedOpened = 0, ownedClosed = 0, closeFailed = 0;
static HANDLE receipt = INVALID_HANDLE_VALUE;
static bool receiptFailed = false;
static void emit(const char* format, ...) {
  char buffer[512]; va_list args; va_start(args, format);
  const int length = std::vsnprintf(buffer, sizeof(buffer), format, args); va_end(args);
  DWORD written = 0;
  if (length < 0 || length >= static_cast<int>(sizeof(buffer)) ||
      !WriteFile(receipt, buffer, static_cast<DWORD>(length), &written, nullptr) || written != static_cast<DWORD>(length)) receiptFailed = true;
}
struct Handle {
  HANDLE value = nullptr;
  explicit Handle(HANDLE h = nullptr) : value(h) { if (value && value != INVALID_HANDLE_VALUE) ++ownedOpened; }
  ~Handle() {
    if (value && value != INVALID_HANDLE_VALUE) {
      if (CloseHandle(value)) ++ownedClosed; else ++closeFailed;
    }
  }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
};

static std::wstring quote(const wchar_t* arg) {
  std::wstring out = L"\""; unsigned slashes = 0;
  for (const wchar_t* p = arg; *p; ++p) {
    if (*p == L'\\') { ++slashes; continue; }
    if (*p == L'\"') { out.append(slashes * 2 + 1, L'\\'); out += *p; }
    else { out.append(slashes, L'\\'); out += *p; }
    slashes = 0;
  }
  out.append(slashes * 2, L'\\'); return out + L'\"';
}

static bool active(HANDLE job, DWORD& count) {
  JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
  if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &info, sizeof(info), nullptr)) return false;
  count = info.ActiveProcesses; return true;
}

static bool stopAndSettle(HANDLE job) {
  if (!TerminateJobObject(job, 125)) return false;
  const auto deadline = GetTickCount64() + 10000;
  DWORD count = 0;
  do {
    if (!active(job, count)) return false;
    if (!count) return true;
    Sleep(20);
  } while (GetTickCount64() < deadline);
  return false;
}

static int waitJob(HANDLE job, HANDLE process, ULONGLONG deadline) {
  DWORD count = 0, exitCode = STILL_ACTIVE; bool rootExited = false, timedOut = false;
  for (;;) {
    if (!rootExited && WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
      if (!GetExitCodeProcess(process, &exitCode)) { emit("error=exitcode settled=%d\n", stopAndSettle(job)); return 125; }
      rootExited = true;
      emit("root_exit=%lu\n", static_cast<unsigned long>(exitCode));
    }
    if (!active(job, count)) { emit("error=accounting settled=%d\n", stopAndSettle(job)); return 125; }
    if (rootExited && count == 0) break;
    if (GetTickCount64() >= deadline) { timedOut = true; break; }
    Sleep(20);
  }
  if (timedOut) {
    if (!TerminateJobObject(job, 124)) { emit("error=terminate settled=unknown\n"); return 125; }
    const auto cleanupDeadline = GetTickCount64() + 10000;
    do {
      if (!active(job, count)) { emit("error=timeout_accounting settled=unknown\n"); return 125; }
      if (!count) break;
      Sleep(20);
    } while (GetTickCount64() < cleanupDeadline);
    if (!rootExited && WaitForSingleObject(process, 0) == WAIT_OBJECT_0 && GetExitCodeProcess(process, &exitCode))
      emit("root_exit_after_termination=%lu\n", static_cast<unsigned long>(exitCode));
    emit("timeout=1 active=%lu\n", static_cast<unsigned long>(count));
    return count ? 125 : 124;
  }
  emit("timeout=0 active=0\n");
  return exitCode <= 123 ? static_cast<int>(exitCode) : 123;
}

static int launch(int argc, wchar_t** argv) {
  wchar_t* end = nullptr; const auto timeout = std::wcstoull(argv[2], &end, 10);
  if (!end || *end || !timeout || timeout > 3600000 || argc < 5) return 125;
  Handle job(CreateJobObjectW(nullptr, nullptr));
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) return 125;
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr));
  HANDLE out = nullptr, err = nullptr;
  if (!DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_OUTPUT_HANDLE), GetCurrentProcess(), &out, 0, TRUE, DUPLICATE_SAME_ACCESS)) return 125;
  Handle output(out);
  if (!DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_ERROR_HANDLE), GetCurrentProcess(), &err, 0, TRUE, DUPLICATE_SAME_ACCESS)) return 125;
  Handle error(err);
  HANDLE handles[] = {input.value, output.value, error.value};
  for (HANDLE h : handles) if (!h || h == INVALID_HANDLE_VALUE) return 125;
  SIZE_T size = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
  std::vector<unsigned char> buffer(size);
  auto attrs = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
  if (!InitializeProcThreadAttributeList(attrs, 1, 0, &size)) return 125;
  const bool ready = UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr) != FALSE;
  STARTUPINFOEXW start{}; start.StartupInfo.cb = sizeof(start); start.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  start.StartupInfo.hStdInput = handles[0]; start.StartupInfo.hStdOutput = handles[1]; start.StartupInfo.hStdError = handles[2];
  start.lpAttributeList = attrs;
  std::wstring command;
  for (int i = 4; i < argc; ++i) { if (!command.empty()) command += L' '; command += quote(argv[i]); }
  PROCESS_INFORMATION pi{};
  const BOOL created = ready && CreateProcessW(argv[4], command.data(), nullptr, nullptr, TRUE,
      CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, argv[3], &start.StartupInfo, &pi);
  DeleteProcThreadAttributeList(attrs);
  if (!created) return 125;
  Handle process(pi.hProcess), thread(pi.hThread);
  bool assigned = false;
#ifdef W1214_TESTING
  if (!GetEnvironmentVariableW(L"W1214_TEST_ASSIGN_FAIL", nullptr, 0))
#endif
    assigned = AssignProcessToJobObject(job.value, process.value) != FALSE;
  if (!assigned) {
    const bool stopped = TerminateProcess(process.value, 125) != FALSE;
    const bool settled = WaitForSingleObject(process.value, 10000) == WAIT_OBJECT_0;
    emit("assign_failed=1 terminated=%d settled=%d\n", stopped, settled);
    return 125;
  }
  emit("root_pid=%lu\n", static_cast<unsigned long>(pi.dwProcessId));
  if (ResumeThread(thread.value) == static_cast<DWORD>(-1)) {
    const bool stopped = TerminateJobObject(job.value, 125) != FALSE;
    const int settlement = waitJob(job.value, process.value, GetTickCount64() + 10000);
    emit("resume_failed=1 terminated=%d settlement=%d\n", stopped, settlement);
    return 125;
  }
#ifdef W1214_TESTING
  if (GetEnvironmentVariableW(L"W1214_TEST_EARLY_CLOSE", nullptr, 0)) {
    if (CloseHandle(job.value)) ++ownedClosed; else ++closeFailed;
    job.value = nullptr;
    const bool settled = WaitForSingleObject(process.value, 10000) == WAIT_OBJECT_0;
    emit("early_close=1 root_settled=%d job_proof=unavailable\n", settled);
    return 125;
  }
#endif
  return waitJob(job.value, process.value, GetTickCount64() + timeout);
}

int wmain(int argc, wchar_t** argv) {
  if (argc < 5) return 125;
  receipt = CreateFileW(argv[1], GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (receipt == INVALID_HANDLE_VALUE) return 125;
  emit("receipt_version=1\n");
  DWORD before = 0, after = 0;
  const bool measuredBefore = GetProcessHandleCount(GetCurrentProcess(), &before) != FALSE;
  int result = launch(argc, argv);
  const bool measuredAfter = GetProcessHandleCount(GetCurrentProcess(), &after) != FALSE;
  emit("handles_before=%lu handles_after=%lu measured=%d\n",
               static_cast<unsigned long>(before), static_cast<unsigned long>(after), measuredBefore && measuredAfter);
  emit("owned_opened=%u owned_closed=%u close_failed=%u\n", ownedOpened, ownedClosed, closeFailed);
  emit("handles_complete=%d\n", !closeFailed && ownedOpened == ownedClosed && !receiptFailed);
  emit("final_result=%d\n", result);
  emit("proof_complete=%d\n", result != 125 && !closeFailed && ownedOpened == ownedClosed && !receiptFailed);
  if (closeFailed || ownedOpened != ownedClosed || receiptFailed || !FlushFileBuffers(receipt)) result = 125;
  if (!CloseHandle(receipt)) result = 125;
  return result;
}
