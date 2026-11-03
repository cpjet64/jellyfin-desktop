// Optional Windows metadata probe. The caller creates and removes an owned
// directory junction; this program neither creates nor follows the target.
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <windows.h>
#include "mpv_test_bootstrap.h"

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  if (argc != 2) return 2;
  const QString root = QDir::fromNativeSeparators(QString::fromLocal8Bit(argv[1]));
  const QString link = QDir(root).filePath("link");
  const QString native = QDir::toNativeSeparators(link);
  const DWORD flags = GetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()));
  if (flags == INVALID_FILE_ATTRIBUTES || !(flags & FILE_ATTRIBUTE_REPARSE_POINT)) return 3;
  QString error;
  if (MpvTestBootstrap::checkCandidateOnDisk(QDir(link).filePath("mpvqt.conf"), root, &error)) return 4;
  qInfo().noquote() << "Rejected task-owned junction:" << error;
  return 0;
}
