#include "mpv_test_bootstrap.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>
#include <cerrno>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace MpvTestBootstrap {
static Entry inspectPath(const QString& path)
{
#ifdef Q_OS_WIN
  const QString nativePath = QDir::toNativeSeparators(path);
  const DWORD flags = GetFileAttributesW(reinterpret_cast<LPCWSTR>(nativePath.utf16()));
  if (flags == INVALID_FILE_ATTRIBUTES) {
    const DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? Entry::Missing : Entry::Error;
  }
  if (flags & FILE_ATTRIBUTE_REPARSE_POINT) return Entry::Link;
  return flags & FILE_ATTRIBUTE_DIRECTORY ? Entry::Directory : Entry::File;
#else
  struct stat info {};
  if (lstat(QFile::encodeName(path).constData(), &info) != 0)
    return errno == ENOENT ? Entry::Missing : Entry::Error;
  if (S_ISLNK(info.st_mode)) return Entry::Link;
  return S_ISDIR(info.st_mode) ? Entry::Directory : (S_ISREG(info.st_mode) ? Entry::File : Entry::Error);
#endif
}

static bool contained(const QString& path, const QString& root)
{
  const QString relative = QDir(root).relativeFilePath(path);
  return relative != ".." && !relative.startsWith("../") && !QDir::isAbsolutePath(relative);
}

static bool supportedAbsolutePath(const QString& path)
{
  if (path.contains(QChar('\0'))) return false;
#ifdef Q_OS_WIN
  if (path.size() < 3 || path.at(1) != ':' || (path.at(2) != '/' && path.at(2) != '\\')) return false;
  const QChar drive = path.at(0);
  return (drive >= 'A' && drive <= 'Z') || (drive >= 'a' && drive <= 'z');
#else
  return QDir::isAbsolutePath(path);
#endif
}

bool checkCandidate(const QString& candidate, const QString& ownedRoot,
                    const Inspector& inspect, QString* error)
{
  if (!supportedAbsolutePath(candidate) || !supportedAbsolutePath(ownedRoot)) {
    if (error) *error = "Unsupported config candidate or owned root path";
    return false;
  }
  const QString absolute = QDir::cleanPath(QDir::fromNativeSeparators(candidate));
#ifdef Q_OS_WIN
  const QString root = absolute.left(3);
#else
  const QString root = "/";
#endif
  if (inspect(root) != Entry::Directory) {
    if (error) *error = "Filesystem root is unavailable or untrusted: " + root;
    return false;
  }
  const QStringList parts = absolute.mid(root.size()).split('/', Qt::SkipEmptyParts);
  QString current = root;
  for (int i = 0; i < parts.size(); ++i) {
    current = QDir(current).filePath(parts[i]);
    const Entry entry = inspect(current);
    if (entry == Entry::Missing) return true;
    if (entry == Entry::Link || entry == Entry::Error ||
        (i + 1 < parts.size() && entry != Entry::Directory)) {
      if (error) *error = "Untrusted or inaccessible path component: " + current;
      return false;
    }
    if (i + 1 == parts.size()) {
      if (entry == Entry::File && contained(absolute, QDir::cleanPath(ownedRoot))) return true;
      if (error) *error = "Existing mpvqt configuration is outside the owned root or is not a file";
      return false;
    }
  }
  if (error) *error = "Empty config candidate";
  return false;
}

bool prepareMpvTestEnvironment(const QString& ownedRoot, QString* error)
{
  if (!QCoreApplication::instance()) {
    if (error) *error = "A Qt application must exist before bootstrap";
    return false;
  }
  if (!supportedAbsolutePath(ownedRoot)) {
    if (error) *error = "Unsupported owned test root path";
    return false;
  }
  const QFileInfo owner(ownedRoot);
  if (!owner.isAbsolute() || inspectPath(owner.absoluteFilePath()) != Entry::Directory ||
      owner.canonicalFilePath().isEmpty()) {
    if (error) *error = "Owned test root is not an existing non-link directory";
    return false;
  }
  if (!checkCandidateOnDisk(QDir(owner.absoluteFilePath()).filePath(".mpv-test-root-probe"),
                            owner.canonicalFilePath(), error)) return false;
#ifdef Q_OS_WIN
  QCoreApplication::setApplicationName("JellyfinDesktopTest-" + QUuid::createUuid().toString(QUuid::Id128));
#endif
#ifdef Q_OS_MAC
  if (qEnvironmentVariable("CI_DEDICATED_GUI_SESSION") != "1") {
    if (error) *error = "macOS tests require an externally verified isolated session";
    return false;
  }
#endif
  const QString config = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
  const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  const QString candidate = QDir(config).filePath("mpvqt/mpvqt.conf");
  qInfo().noquote() << "Qt config:" << config << "Qt cache:" << cache << "MpvQt config candidate:" << candidate;
  if (config.isEmpty() || cache.isEmpty()) {
    if (error) *error = "Qt did not provide config and cache locations";
    return false;
  }
#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
  if (!contained(QDir::cleanPath(config), owner.canonicalFilePath()) ||
      !contained(QDir::cleanPath(cache), owner.canonicalFilePath())) {
    if (error) *error = "Linux Qt config/cache must be under the owned test root";
    return false;
  }
  if (!checkCandidateOnDisk(QDir(cache).filePath(".mpv-test-cache-probe"), owner.canonicalFilePath(), error))
    return false;
#endif
  return checkCandidateOnDisk(candidate, owner.canonicalFilePath(), error);
}

bool checkCandidateOnDisk(const QString& candidate, const QString& ownedRoot, QString* error)
{
  return checkCandidate(candidate, ownedRoot, inspectPath, error);
}
}
