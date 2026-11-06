#include "NativePlaybackObservation.h"
#include <QLocale>
#include <cmath>
#include <limits>

const char* NativePlaybackObservation::expression()
{
  return "NR1|${=playlist/count:ERR}|${=playlist/0/id:ERR}|${=playlist/0/playing:ERR}|"
         "${=playlist/0/current:ERR}|${=time-pos:ERR}|${=pause:ERR}|${=idle-active:ERR}|${=seeking:ERR}";
}

static bool decimal(const QByteArray& text)
{
  if (text.isEmpty()) return false;
  bool digit = false, dot = false;
  for (const char c : text) {
    if (c >= '0' && c <= '9') digit = true;
    else if (c == '.' && !dot) dot = true;
    else return false;
  }
  return digit;
}

bool NativePlaybackObservation::decode(const QByteArray& reply, qint64 expectedEntry, qint64 durationMs,
                                       bool expectedPaused, qint64& positionMs)
{
  if (reply.size() > MaxReplyBytes || expectedEntry <= 0 || durationMs < 0) return false;
  const auto fields = reply.split('|');
  if (fields.size() != 9 || fields[0] != "NR1" || fields[1] != "1" ||
      fields[2] != QByteArray::number(expectedEntry) || fields[3] != "yes" || fields[4] != "yes" ||
      fields[6] != (expectedPaused ? "yes" : "no") || fields[7] != "no" || fields[8] != "no" ||
      !decimal(fields[5])) return false;
  bool ok = false;
  const double seconds = QLocale::c().toDouble(QString::fromLatin1(fields[5]), &ok);
  const double milliseconds = seconds * 1000.0;
  if (!ok || !std::isfinite(milliseconds) || milliseconds < 0 ||
      milliseconds > double(durationMs) || milliseconds > double(std::numeric_limits<qint64>::max() / 2)) return false;
  positionMs = qRound64(milliseconds);
  return positionMs <= durationMs;
}
