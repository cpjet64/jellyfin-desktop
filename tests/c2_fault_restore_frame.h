#pragma once

#include <QImage>
#include <cmath>

// The generated 160x90 clip places nine 12x12 counter blocks at y=8..19.
// Sample block centers in the displayed image; reject ambiguous gray values.
inline int c2RestoreFrameIndex(const QImage& image)
{
  if (image.width() < 160 || image.height() < 90) return -1;
  int value = 0;
  for (int bit = 0; bit < 9; ++bit) {
    const int x = (11 + bit * 16) * image.width() / 160;
    const int y = 14 * image.height() / 90;
    const int gray = qGray(image.pixel(x, y));
    if (gray > 110 && gray < 180) return -1;
    if (gray >= 180) value |= 1 << bit;
  }
  return value;
}

inline bool c2RestorePositionMatches(double actual, double snapshot)
{
  return std::isfinite(actual) && std::isfinite(snapshot) && std::abs(actual - snapshot) <= 0.100;
}

inline bool c2RestoreFrameMatches(const QImage& image, double positionSeconds)
{
  const int index = c2RestoreFrameIndex(image);
  return index >= 0 && c2RestorePositionMatches(double(index) / 30.0, positionSeconds);
}
