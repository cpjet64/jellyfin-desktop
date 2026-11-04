#pragma once

#include <QString>
#include <QUrl>

enum class C2PathPlatform { Windows, Posix };

struct C2PathValue
{
  QString value;
  QString kind = QStringLiteral("unsupported");
  bool valid = false;
};

inline bool c2DrivePath(const QString& path)
{
  return path.size() >= 3 && path.at(0).isLetter() && path.at(0).unicode() < 128 &&
    path.at(1) == QLatin1Char(':') && (path.at(2) == QLatin1Char('/') || path.at(2) == QLatin1Char('\\'));
}

inline C2PathValue c2PathValue(const QString& input, C2PathPlatform platform)
{
  if (input.isEmpty() || input.size() > 4096 || input.contains(QChar::Null)) return {};
  QString path = input;
  bool fileUrl = false;
  if (input.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
    const QUrl url = QUrl::fromEncoded(input.toUtf8(), QUrl::StrictMode);
    if (!url.isValid() || !url.isLocalFile() || !url.host().isEmpty() || url.hasQuery() || url.hasFragment()) return {};
    path = url.path(QUrl::FullyDecoded);
    fileUrl = true;
    if (platform == C2PathPlatform::Windows && path.startsWith('/') && c2DrivePath(path.mid(1))) path.remove(0, 1);
  }
  if (path.contains(QChar::Null)) return {};
  if (platform == C2PathPlatform::Windows) {
    if (!c2DrivePath(path) || path.indexOf(QLatin1Char(':'), 2) >= 0) return {};
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
  } else {
    if (!path.startsWith('/') || path.startsWith("//")) return {};
  }
  return {path, fileUrl ? QStringLiteral("local-file-url") :
    (platform == C2PathPlatform::Windows ? QStringLiteral("windows-drive") : QStringLiteral("posix-absolute")), true};
}

inline C2PathPlatform c2HostPathPlatform()
{
#ifdef Q_OS_WIN
  return C2PathPlatform::Windows;
#else
  return C2PathPlatform::Posix;
#endif
}

inline bool c2SamePath(const QString& actual, const QString& expected, C2PathPlatform platform)
{
  const auto a = c2PathValue(actual, platform);
  const auto b = c2PathValue(expected, platform);
  return a.valid && b.valid && a.value == b.value;
}
