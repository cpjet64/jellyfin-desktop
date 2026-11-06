#include "WindowsFrameReader.h"
#ifdef _WIN32
#include <QtEndian>

namespace FixtureRestart {
IoProgress WindowsFrameReader::begin(ULONGLONG deadline)
{
  if (stage_ != Stage::Idle) return IoProgress::Failed;
  deadline_ = deadline;
  stage_ = Stage::Header;
  return advance(io_.begin(pipe_, true, nullptr, 4, deadline_));
}

IoProgress WindowsFrameReader::advance(IoProgress result)
{
  while (result == IoProgress::Complete || result == IoProgress::Eof) {
    if (stage_ == Stage::Eof) {
      stage_ = result == IoProgress::Eof ? Stage::Done : Stage::Failed;
      return stage_ == Stage::Done ? IoProgress::Complete : IoProgress::Failed;
    }
    if (result == IoProgress::Eof) { stage_ = Stage::Failed; return IoProgress::Failed; }
    const auto& bytes = io_.bytes();
    frame_.append(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size()));
    if (stage_ == Stage::Header) {
      const quint32 length = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(frame_.constData()));
      if (!length || length > 9212) { stage_ = Stage::Failed; return IoProgress::Failed; }
      stage_ = Stage::Body;
      result = io_.begin(pipe_, true, nullptr, length, deadline_);
    } else if (stage_ == Stage::Body) {
      stage_ = Stage::Eof;
      result = io_.begin(pipe_, true, nullptr, 1, deadline_);
    } else { stage_ = Stage::Failed; return IoProgress::Failed; }
  }
  if (result == IoProgress::Failed) stage_ = Stage::Failed;
  return result;
}

IoProgress WindowsFrameReader::poll()
{
  if (stage_ == Stage::Done) return IoProgress::Complete;
  if (stage_ == Stage::Idle || stage_ == Stage::Failed) return IoProgress::Failed;
  const auto result = io_.poll();
  if (canceled_) {
    if (result != IoProgress::Pending && result != IoProgress::Unconfirmed) stage_ = Stage::Failed;
    return stage_ == Stage::Failed ? IoProgress::Failed : result;
  }
  return advance(result);
}

IoProgress WindowsFrameReader::cancel()
{
  if (stage_ == Stage::Idle || stage_ == Stage::Done || stage_ == Stage::Failed) return IoProgress::Failed;
  canceled_ = true;
  const IoProgress result = io_.cancel();
  if (result != IoProgress::Unconfirmed && result != IoProgress::Pending) stage_ = Stage::Failed;
  return result == IoProgress::Complete || result == IoProgress::Eof ? IoProgress::Failed : result;
}
}
#endif
