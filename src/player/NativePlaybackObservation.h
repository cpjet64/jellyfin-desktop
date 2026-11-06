#pragma once

#include <QByteArray>
#include <QtGlobal>

// A single bounded reply from the fixed, core-serialized expand-text command.
class NativePlaybackObservation
{
public:
  static constexpr int MaxReplyBytes = 256;
  static const char* expression();
  static bool decode(const QByteArray& reply, qint64 expectedEntry, qint64 durationMs,
                     bool expectedPaused, qint64& positionMs);
};
