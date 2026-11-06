#include "fixture_restart/WindowsFrameReader.h"
#ifdef _WIN32
#include <QtEndian>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#define CHECK(value) do { if (!(value)) std::abort(); } while (false)
using namespace FixtureRestart;

namespace {
QByteArray input;
qsizetype offset = 0;
DWORD lastError = ERROR_BROKEN_PIPE;
bool pending = false, complete = false;
HANDLE WINAPI eventFake(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR) { return reinterpret_cast<HANDLE>(1); }
BOOL WINAPI closeFake(HANDLE) { return TRUE; }
BOOL WINAPI resetFake(HANDLE) { return TRUE; }
ULONGLONG WINAPI clockFake() { return 1; }
DWORD WINAPI errorFake() { return lastError; }
BOOL WINAPI cancelFake(HANDLE, LPOVERLAPPED) { return TRUE; }
BOOL WINAPI resultFake(HANDLE, LPOVERLAPPED, LPDWORD count, BOOL)
{
  if (!complete) { lastError = ERROR_IO_INCOMPLETE; return FALSE; }
  *count = 1;
  return TRUE;
}
BOOL WINAPI readFake(HANDLE, LPVOID out, DWORD size, LPDWORD count, LPOVERLAPPED)
{
  if (pending) { lastError = ERROR_IO_PENDING; return FALSE; }
  if (offset == input.size()) { lastError = ERROR_BROKEN_PIPE; return FALSE; }
  const auto n = std::min<qsizetype>(size, input.size() - offset);
  std::memcpy(out, input.constData() + offset, size_t(n));
  offset += n; *count = DWORD(n); return TRUE;
}
WindowsIoApi api()
{
  WindowsIoApi value;
  value.createEvent = eventFake; value.closeHandle = closeFake; value.resetEvent = resetFake;
  value.now = clockFake; value.error = errorFake; value.readFile = readFake;
  value.result = resultFake; value.cancel = cancelFake;
  return value;
}
QByteArray encoded(const QByteArray& body)
{
  QByteArray frame(4, '\0');
  qToBigEndian<quint32>(quint32(body.size()), reinterpret_cast<uchar*>(frame.data()));
  return frame + body;
}
void framing()
{
  for (int mode = 0; mode != 4; ++mode) {
    input = encoded("hello"); offset = 0;
    if (mode == 1) input.chop(1);
    if (mode == 2) input += 'x';
    if (mode == 3) input = QByteArray(4, '\0');
    auto* io = new WindowsRestartIo(api());
    WindowsFrameReader reader(*io, reinterpret_cast<HANDLE>(2));
    const auto result = reader.begin(100);
    CHECK(result == (mode == 0 ? IoProgress::Complete : IoProgress::Failed));
    CHECK((reader.completedFrame() != nullptr) == (mode == 0));
    if (mode == 0) CHECK(*reader.completedFrame() == input);
    CHECK(io->release());
  }
}
void cancellation()
{
  pending = true; complete = false;
  auto* io = new WindowsRestartIo(api());
  WindowsFrameReader reader(*io, reinterpret_cast<HANDLE>(2));
  CHECK(reader.begin(100) == IoProgress::Pending);
  CHECK(reader.cancel() == IoProgress::Unconfirmed && !io->release());
  complete = true;
  CHECK(reader.poll() == IoProgress::Failed && !reader.completedFrame());
  CHECK(io->release());
  pending = false;
}
}
int main() { framing(); cancellation(); return 0; }
#else
int main() { return 0; }
#endif
