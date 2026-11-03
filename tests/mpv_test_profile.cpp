#include "mpv_test_profile.h"
#include "mpv_test_bootstrap.h"

#include <QDir>
#include <QFileInfo>
#include "core/ProfileManager.h"
#include "shared/Paths.h"

static bool ownedDirectory(const QString& path, const QString& root)
{
  const QFileInfo info(path);
  const QString actual = info.canonicalFilePath();
  if (actual.isEmpty() || !info.isDir() || info.isSymLink()) return false;
  const QString relative = QDir(root).relativeFilePath(actual);
  return relative != ".." && !relative.startsWith("../") && !QDir::isAbsolutePath(relative);
}

static bool containedPath(const QString& path, const QString& root)
{
  if (!QDir::isAbsolutePath(path) || !QDir::isAbsolutePath(root)) return false;
  const QString relative = QDir(QDir::cleanPath(root)).relativeFilePath(QDir::cleanPath(path));
  return relative != ".." && !relative.startsWith("../") && !QDir::isAbsolutePath(relative);
}

bool prepareMpvTestProfile(const QString& ownedRoot, QString* error)
{
  const QString root = QFileInfo(ownedRoot).canonicalFilePath();
  if (root.isEmpty() || !QFileInfo(root).isDir()) {
    if (error) *error = "Owned profile root is not an existing directory";
    return false;
  }
  if (!MpvTestBootstrap::checkCandidateOnDisk(QDir(ownedRoot).filePath(".mpv-profile-probe"), root, error))
    return false;
  const QString config = QDir(root).filePath("config");
  const QString cache = QDir(root).filePath("cache");
  if (!QDir().mkpath(config) || !QDir().mkpath(cache) ||
      !ownedDirectory(config, root) || !ownedDirectory(cache, root) ||
      !MpvTestBootstrap::checkCandidateOnDisk(QDir(config).filePath(".mpv-profile-probe"), root, error) ||
      !MpvTestBootstrap::checkCandidateOnDisk(QDir(cache).filePath(".mpv-profile-probe"), root, error)) {
    if (error) *error = "Could not create safe profile roots";
    return false;
  }
  Paths::setConfigDir(config);
  Paths::setCacheDir(cache);
  const QString dataDir = ProfileManager::activeProfile().dataDir();
  const QString cacheDir = ProfileManager::activeProfile().cacheDir();
  if (!containedPath(dataDir, config) || !containedPath(cacheDir, cache) ||
      !MpvTestBootstrap::checkCandidateOnDisk(QDir(dataDir).filePath(".mpv-profile-probe"), root, error) ||
      !MpvTestBootstrap::checkCandidateOnDisk(QDir(cacheDir).filePath(".mpv-profile-probe"), root, error) ||
      !QDir().mkpath(dataDir) || !QDir().mkpath(cacheDir) ||
      !ownedDirectory(dataDir, root) || !ownedDirectory(cacheDir, root)) {
    if (error) *error = "Resolved profile directories escaped or could not be created";
    return false;
  }
  return true;
}
