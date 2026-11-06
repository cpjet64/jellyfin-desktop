#pragma once
#include <QDir>
#include <QFileInfo>

inline QString ownedFixtureTemplate()
{
  const QFileInfo root(QString::fromLocal8Bit(qgetenv("JELLYFIN_TEST_OWNED_ROOT")));
  if (!root.isAbsolute() || !root.isDir() || root.isSymLink()) return {};
  return QDir(root.canonicalFilePath()).filePath(QStringLiteral("restart-fixture-XXXXXX"));
}
