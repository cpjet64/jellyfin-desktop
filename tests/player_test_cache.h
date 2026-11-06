#pragma once

#include "shared/Paths.h"
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <memory>

// Each test process owns its cache before constructing a PlayerComponent.
class PlayerTestCache
{
public:
  PlayerTestCache()
  {
    const QFileInfo root(QString::fromLocal8Bit(qgetenv("JELLYFIN_TEST_OWNED_ROOT")));
    if (!root.isAbsolute() || !root.isDir() || root.isSymLink())
      qFatal("Player test requires an owned cache root");
    const QString canonical = root.canonicalFilePath();
    if (canonical.isEmpty()) qFatal("Player test cache root is unavailable");
    cache_ = std::make_unique<QTemporaryDir>(QDir(canonical).filePath("player-cache-XXXXXX"));
    if (!cache_->isValid()) qFatal("Player test cache could not be created");
    const QFileInfo actual(cache_->path());
    if (!actual.isDir() || actual.isSymLink() || QFileInfo(actual.canonicalPath()).canonicalFilePath() != canonical)
      qFatal("Player test cache escaped its owned root");
    Paths::setCacheDir(actual.canonicalFilePath());
  }

  ~PlayerTestCache() { Paths::setCacheDir(QString()); }

private:
  std::unique_ptr<QTemporaryDir> cache_;
};
