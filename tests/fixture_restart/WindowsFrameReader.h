#pragma once
#ifdef _WIN32
#include "WindowsRestartIo.h"
#include <QByteArray>

namespace FixtureRestart {
// The process allocation owns the I/O object and pipe for this reader's entire
// lifetime. Unconfirmed I/O must remain owned by the outer containment owner.
class WindowsFrameReader {
public:
  WindowsFrameReader(WindowsRestartIo& io, HANDLE pipe) : io_(io), pipe_(pipe) {}
  IoProgress begin(ULONGLONG deadline);
  IoProgress poll();
  IoProgress cancel();
  const QByteArray* completedFrame() const { return stage_ == Stage::Done ? &frame_ : nullptr; }
private:
  enum class Stage { Idle, Header, Body, Eof, Done, Failed };
  IoProgress advance(IoProgress result);
  WindowsRestartIo& io_;
  HANDLE pipe_;
  ULONGLONG deadline_ = 0;
  Stage stage_ = Stage::Idle;
  QByteArray frame_;
  bool canceled_ = false;
};
}
#endif
