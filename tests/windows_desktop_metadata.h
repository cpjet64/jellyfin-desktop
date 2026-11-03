#ifndef WINDOWS_DESKTOP_METADATA_H
#define WINDOWS_DESKTOP_METADATA_H

#include <QJsonObject>
#ifdef Q_OS_WIN
#include <windows.h>
#include <vector>

inline QJsonObject userObjectName(HANDLE object)
{
  DWORD bytes = 0;
  if (!object) return {{"error", static_cast<qint64>(GetLastError())}};
  if (!GetUserObjectInformationW(object, UOI_NAME, nullptr, 0, &bytes) &&
      GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    return {{"error", static_cast<qint64>(GetLastError())}};
  if (!bytes) return {{"error", "empty name buffer"}};
  std::vector<wchar_t> name((bytes + sizeof(wchar_t) - 1) / sizeof(wchar_t), 0);
  if (!GetUserObjectInformationW(object, UOI_NAME, name.data(), bytes, &bytes))
    return {{"error", static_cast<qint64>(GetLastError())}};
  return {{"name", QString::fromWCharArray(name.data())}};
}

inline QJsonObject windowsDesktopMetadata()
{
  QJsonObject result;
  DWORD session = 0;
  if (ProcessIdToSessionId(GetCurrentProcessId(), &session)) result["sessionId"] = static_cast<qint64>(session);
  else result["sessionError"] = static_cast<qint64>(GetLastError());
  HWINSTA station = GetProcessWindowStation();
  result["windowStation"] = userObjectName(station);
  USEROBJECTFLAGS flags{};
  DWORD needed = 0;
  if (station && GetUserObjectInformationW(station, UOI_FLAGS, &flags, sizeof(flags), &needed))
    result["windowStationVisible"] = (flags.dwFlags & WSF_VISIBLE) != 0;
  else result["windowStationVisibilityError"] = static_cast<qint64>(GetLastError());
  result["threadDesktop"] = userObjectName(GetThreadDesktop(GetCurrentThreadId()));
  return result;
}
#else
inline QJsonObject windowsDesktopMetadata() { return {{"available", false}}; }
#endif

#endif
