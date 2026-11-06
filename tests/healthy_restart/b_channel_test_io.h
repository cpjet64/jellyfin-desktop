#pragma once

#include "module_endpoints.h"
#include <algorithm>
#include <cstring>
#include <vector>

// Script only OS outcomes; protocol parsing and endpoint ownership stay real.
struct BChannelIo {
  struct Pending { HANDLE pipe = nullptr; void* bytes = nullptr; DWORD size = 0; OVERLAPPED* overlap = nullptr; };
  QByteArray grant, control;
  std::vector<QByteArray> writes;
  Pending pending[2];
  DWORD error = ERROR_SUCCESS;
  bool endpointValid = true;
};

inline BChannelIo* bChannelScript = nullptr;
inline HANDLE WINAPI bChannelEvent(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR) { return reinterpret_cast<HANDLE>(99); }
inline BOOL WINAPI bChannelYes(HANDLE) { return TRUE; }
inline ULONGLONG WINAPI bChannelNow() { return 1; }
inline DWORD WINAPI bChannelError() { return bChannelScript->error; }
inline BOOL WINAPI bChannelCancel(HANDLE, LPOVERLAPPED) { return TRUE; }

inline bool bChannelDeliver(HANDLE pipe, void* output, DWORD size, DWORD* count)
{
  auto& data = pipe == reinterpret_cast<HANDLE>(101) ? bChannelScript->grant : bChannelScript->control;
  if (data.isEmpty()) {
    *count = 0;
    bChannelScript->error = pipe == reinterpret_cast<HANDLE>(101) ? ERROR_BROKEN_PIPE : ERROR_IO_PENDING;
    return false;
  }
  const auto n = std::min<DWORD>(size, DWORD(data.size()));
  std::memcpy(output, data.constData(), n);
  data.remove(0, n);
  *count = n;
  return true;
}

inline BOOL WINAPI bChannelRead(HANDLE pipe, LPVOID output, DWORD size, LPDWORD count, LPOVERLAPPED overlap)
{
  if (bChannelDeliver(pipe, output, size, count)) return TRUE;
  if (bChannelScript->error == ERROR_IO_PENDING)
    bChannelScript->pending[pipe == reinterpret_cast<HANDLE>(101) ? 0 : 1] = {pipe, output, size, overlap};
  return FALSE;
}

inline BOOL WINAPI bChannelResult(HANDLE pipe, LPOVERLAPPED overlap, LPDWORD count, BOOL)
{
  auto& pending = bChannelScript->pending[pipe == reinterpret_cast<HANDLE>(101) ? 0 : 1];
  if (pending.overlap != overlap) { bChannelScript->error = ERROR_INVALID_HANDLE; return FALSE; }
  if (!bChannelDeliver(pipe, pending.bytes, pending.size, count)) {
    if (bChannelScript->error == ERROR_IO_PENDING) bChannelScript->error = ERROR_IO_INCOMPLETE;
    return FALSE;
  }
  pending = {};
  return TRUE;
}

inline BOOL WINAPI bChannelWrite(HANDLE, LPCVOID input, DWORD size, LPDWORD count, LPOVERLAPPED)
{
  bChannelScript->writes.emplace_back(static_cast<const char*>(input), int(size));
  *count = size;
  return TRUE;
}

inline HealthyModuleIdentity::ChildEndpoints::TestOperations bChannelEndpointOperations()
{
  using namespace HealthyModuleIdentity;
  ChildEndpoints::TestOperations ops;
  ops.valid = [] { return bChannelScript && bChannelScript->endpointValid; };
  ops.accepts = [](std::uintptr_t value) { return value != 0; };
  ops.compare = [](std::uintptr_t a, std::uintptr_t b) {
    return a == b ? EndpointComparison::Same : EndpointComparison::Different;
  };
  ops.duplicate = [](std::uintptr_t value, HANDLE& out) { out = reinterpret_cast<HANDLE>(value + 100); return true; };
  ops.close = [](HANDLE) { return true; };
  ops.io.createEvent = bChannelEvent; ops.io.closeHandle = bChannelYes; ops.io.resetEvent = bChannelYes;
  ops.io.now = bChannelNow; ops.io.readFile = bChannelRead; ops.io.writeFile = bChannelWrite;
  ops.io.result = bChannelResult; ops.io.cancel = bChannelCancel; ops.io.error = bChannelError;
  return ops;
}
