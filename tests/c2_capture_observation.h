#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <cmath>
#include "c2_capture_path.h"

struct C2CaptureObservation
{
  int pathStatus = -1, timeStatus = -1, pauseStatus = -1;
  QString rawPath, expectedPath;
  bool pathPresent = false, pathTruncated = false;
  double position = 0;
  int paused = 0;
  qint64 entry = 0;
  C2PathPlatform platform = c2HostPathPlatform();

  QJsonArray failures() const
  {
    QJsonArray result;
    if (pathStatus < 0 || !pathPresent || pathTruncated) result.append("path-property");
    if (!c2SamePath(rawPath, expectedPath, platform)) result.append("path-mismatch");
    if (entry <= 0) result.append("entry");
    if (timeStatus < 0) result.append("time-property");
    if (!std::isfinite(position) || position < 0) result.append("time-value");
    if (pauseStatus < 0) result.append("pause-property");
    return result;
  }
  bool accepted() const { return failures().isEmpty(); }
  QJsonObject record() const
  {
    const auto path = c2PathValue(rawPath, platform);
    const auto expected = c2PathValue(expectedPath, platform);
    return {{"sampling", "sequential-gui-properties"}, {"path_status", pathStatus},
      {"raw_path", rawPath.left(4096)}, {"raw_path_truncated", pathTruncated}, {"path_present", pathPresent},
      {"expected_path", expectedPath.left(4096)}, {"representation", path.kind},
      {"expected_representation", expected.kind}, {"path_matches", c2SamePath(rawPath, expectedPath, platform)},
      {"entry", entry}, {"entry_valid", entry > 0}, {"time_status", timeStatus},
      {"time_value", timeStatus >= 0 && std::isfinite(position) ? QJsonValue(position) : QJsonValue(QJsonValue::Null)},
      {"time_class", timeStatus < 0 ? "unavailable" : (std::isfinite(position) ? "finite" : "nonfinite")},
      {"pause_status", pauseStatus}, {"pause_value", pauseStatus >= 0 ? QJsonValue(paused) : QJsonValue(QJsonValue::Null)},
      {"accepted", accepted()}, {"failed_predicates", failures()}};
  }
};
