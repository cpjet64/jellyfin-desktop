#ifndef CHROMIUMFLAGS_H
#define CHROMIUMFLAGS_H

#include <QByteArray>
#include <QProcess>
#include <QStringList>

namespace ChromiumFlags
{
inline bool hasSwitch(const QStringList& tokens, const QString& name)
{
  for (const QString& token : tokens)
    if (token == name || token.startsWith(name + '='))
      return true;
  return false;
}

inline QByteArray compose(const QByteArray& existing, bool disableGpu,
                          bool disablePinch, bool ignoreCertificates, bool linuxPlatform)
{
  const QStringList userFlags = QProcess::splitCommand(QString::fromUtf8(existing));
  QStringList additions;
  if (disableGpu && !hasSwitch(userFlags, "--disable-gpu"))
    additions << "--disable-gpu";
  if (!disableGpu && !hasSwitch(userFlags, "--disable-gpu") &&
      !hasSwitch(userFlags, "--enable-gpu-rasterization") &&
      !hasSwitch(userFlags, "--disable-gpu-rasterization"))
    additions << "--enable-gpu-rasterization";
  if (!hasSwitch(userFlags, "--disable-features"))
    additions << (linuxPlatform ? "--disable-features=MediaSessionService,HardwareMediaKeyHandling"
                                : "--disable-features=MediaSessionService");
  if (disablePinch && !hasSwitch(userFlags, "--disable-pinch") &&
      !hasSwitch(userFlags, "--enable-pinch"))
    additions << "--disable-pinch";
  if (ignoreCertificates && !hasSwitch(userFlags, "--ignore-certificate-errors"))
    additions << "--ignore-certificate-errors";
  QByteArray result = existing;
  if (!additions.isEmpty())
  {
    if (!result.isEmpty())
      result += ' ';
    result += additions.join(' ').toUtf8();
  }
  return result;
}
}

#endif
