#pragma once

#include <QByteArray>
#include <QFile>

// The file remains present on a partial write; no retry may overwrite it.
template <typename Open, typename Write, typename Flush>
bool persistSynchronousTrace(const QByteArray& data, Open open, Write write, Flush flush)
{
  if (!open()) return false;
  if (write(data) != data.size()) return false;
  return flush();
}

inline bool persistSynchronousTraceFile(const QString& path, const QByteArray& data)
{
  QFile file(path);
  return persistSynchronousTrace(data,
    [&] { return file.open(QIODevice::WriteOnly | QIODevice::NewOnly); },
    [&](const QByteArray& bytes) { return file.write(bytes); }, [&] { return file.flush(); });
}
