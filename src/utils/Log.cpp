//
// Created by Tobias Hieta on 07/03/16.
//

#include "Log.h"

#include <QtQml>
#include <QGuiApplication>
#include <iostream>

#include "shared/Names.h"
#include "shared/Paths.h"
#include "core/ProfileManager.h"
#include "settings/SettingsComponent.h"
#include "Version.h"

int fileLogLevel = 1;  // info
int terminalLogLevel = 3;  // error
QHash<QtMsgType, int> messageLevelValue({{QtDebugMsg, 0}, {QtInfoMsg, 1}, {QtWarningMsg, 2}, {QtCriticalMsg, 3}, {QtFatalMsg, 4}});

static QMutex logMutex;
static QFile* logFile = nullptr;
static QString tempLogPath;
static thread_local bool inLogOperation = false;

// Qt file operations can themselves emit messages. Do not re-enter the mutex
// in that case; diagnostics still reach the terminal.
class LogOperation
{
public:
  LogOperation() : lock(&logMutex) { inLogOperation = true; }
  ~LogOperation() { inLogOperation = false; }
private:
  QMutexLocker<QMutex> lock;
};

/////////////////////////////////////////////////////////////////////////////////////////
// adapted from https://stackoverflow.com/a/62390212
static void qtMessageOutput(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
  if (inLogOperation)
  {
    QString safeMessage = msg;
    Log::CensorAuthTokens(safeMessage);
    std::cerr << qPrintable(safeMessage) << std::endl;
    if (type == QtFatalMsg)
      abort();
    return;
  }
  LogOperation lock;

  // Check if message meets any output threshold
  bool shouldOutputToTerminal = messageLevelValue[type] >= terminalLogLevel;
  bool shouldOutputToFile = messageLevelValue[type] >= fileLogLevel;

  if (!shouldOutputToTerminal && !shouldOutputToFile && type != QtFatalMsg)
    return;

  if (shouldOutputToTerminal) {
    QString terminalMsg;
    if (terminalLogLevel == 0) {  // debug level
      terminalMsg = qFormatLogMessage(type, context, msg);
    } else {
      terminalMsg = msg;
    }
    Log::CensorAuthTokens(terminalMsg);
    std::cerr << qPrintable(terminalMsg) << std::endl;
  }

  if (logFile && logFile->isOpen() && shouldOutputToFile) {
    QString fileMessage = qFormatLogMessage(type, context, msg);
    Log::CensorAuthTokens(fileMessage);
    logFile->write(fileMessage.toUtf8() + '\n');
    logFile->flush();
  }

  if (type == QtFatalMsg)
    abort();
}

/////////////////////////////////////////////////////////////////////////////////////////
static void elidePattern(QString& msg, const QString& substring, qsizetype chars)
{
  qsizetype start = 0;
  while (true)
  {
    start = msg.indexOf(substring, start);
    if (start < 0 || start + substring.length() + chars > msg.length())
      break;
    start += substring.length();
    for (qsizetype n = 0; n < chars; n++)
      msg[start + n] = QChar('x');
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void Log::CensorAuthTokens(QString& msg)
{
  elidePattern(msg, "api_key=", 32);
  elidePattern(msg, "X-MediaBrowser-Token%3D", 32);
  elidePattern(msg, "X-MediaBrowser-Token=", 32);
  elidePattern(msg, "api_key=", 32);
  elidePattern(msg, "ApiKey=", 32);
  elidePattern(msg, "AccessToken=", 32);
  elidePattern(msg, "AccessToken\":\"", 32);
}


/////////////////////////////////////////////////////////////////////////////////////////
static QString getLogDir()
{
  return ProfileManager::activeProfile().logDir();
}

/////////////////////////////////////////////////////////////////////////////////////////
void Log::Init()
{
  qSetMessagePattern("%{time yyyy-MM-dd hh:mm:ss.zzz} [%{type}] %{function} @ %{line} - %{message}");
  qInstallMessageHandler(qtMessageOutput);

  // Create unique log file for this instance
  QString logDir = getLogDir();
  auto* tempFile = new QTemporaryFile(logDir + "/jellyfin-desktop-XXXXXX.log");
  tempFile->setAutoRemove(false);
  if (!tempFile->open())
  {
    qFatal("Failed to create temporary log file");
  }
  QString path = tempFile->fileName();
  QFile* activeFile = tempFile;
  if (!path.isEmpty())
  {
    // QTemporaryFile can retain a native handle after close, preventing a
    // separate QFile::rename on Windows. Destroy it before reopening by name.
    delete tempFile;
    activeFile = new QFile(path);
    if (!activeFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append))
      qFatal("Failed to reopen temporary log file");
  }
  {
    LogOperation lock;
    delete logFile;
    logFile = activeFile;
    tempLogPath = path;
  }
  if (path.isEmpty())
    qWarning("Temporary log file has no name; keeping it open without rotation");

  qInfo() << "Starting Jellyfin version:" << qPrintable(Version::GetVersionString()) << "build date:" << qPrintable(Version::GetBuildDate());
  qInfo() << qPrintable(QString("  Running on: %1 [%2] arch %3").arg(QSysInfo::prettyProductName()).arg(QSysInfo::kernelVersion()).arg(QSysInfo::currentCpuArchitecture()));
  qInfo() << "  Qt Version:" << QT_VERSION_STR << qPrintable(QString("[%1]").arg(QSysInfo::buildAbi()));

  qDebug() << "Logging to " << qPrintable(getLogDir() + "/" + Names::DataName() + ".log");
}

/////////////////////////////////////////////////////////////////////////////////////////
static bool rotateArchive(const QString& from, const QString& to)
{
  if (!QFile::exists(to))
    return QFile::rename(from, to);

  // Keep the destination until its replacement has been moved successfully.
  QTemporaryFile backup(to + ".backup-XXXXXX");
  backup.setAutoRemove(false);
  if (!backup.open())
    return false;
  const QString backupPath = backup.fileName();
  backup.close();
  if (backupPath.isEmpty() || !QFile::remove(backupPath))
    return false;
  if (!QFile::rename(to, backupPath))
    return false;
  if (!QFile::rename(from, to))
  {
    if (!QFile::rename(backupPath, to))
      std::cerr << "Failed to restore log archive; backup retained" << std::endl;
    return false;
  }
  if (!QFile::remove(backupPath))
    std::cerr << "Failed to remove old log archive backup" << std::endl;
  return true;
}

/////////////////////////////////////////////////////////////////////////////////////////
void Log::RotateLog()
{
  LogOperation lock;

  if (!logFile || !logFile->isOpen() || tempLogPath.isEmpty())
    return;

  QString baseName = Names::DataName() + ".log";
  QString mainLog = getLogDir() + "/" + baseName;

  // Rotate existing logs: .log.2 -> .log.3, .log.1 -> .log.2, .log -> .log.1
  const int maxLogs = 10;
  for (int i = maxLogs - 1; i >= 1; i--)
  {
    QString from = mainLog + "." + QString::number(i);
    QString to = mainLog + "." + QString::number(i + 1);
    if (QFile::exists(from))
    {
      if (!rotateArchive(from, to))
      {
        std::cerr << "Failed to rotate log archive" << std::endl;
        return;
      }
    }
  }

  // Rotate main log to .log.1
  if (QFile::exists(mainLog))
  {
    if (!QFile::rename(mainLog, mainLog + ".1"))
    {
      std::cerr << "Failed to rotate main log" << std::endl;
      return;
    }
  }

  // Rename unique log to main log
  logFile->flush();
  logFile->close();
  QFile temporaryLog(tempLogPath);
  if (!temporaryLog.rename(mainLog))
  {
    if (!logFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append))
      std::cerr << "Failed to reopen temporary log" << std::endl;
    std::cerr << "Failed to promote temporary log: " << qPrintable(temporaryLog.errorString()) << std::endl;
    return;
  }

  // Reopen as main log and continue writing
  delete logFile;
  logFile = new QFile(mainLog);
  if (!logFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append))
    std::cerr << "Failed to open main log" << std::endl;
  tempLogPath.clear();
}

/////////////////////////////////////////////////////////////////////////////////////////
void Log::Cleanup()
{
  LogOperation lock;

  // Close and delete temp log file
  if (logFile && logFile->isOpen())
    logFile->close();

  delete logFile;
  logFile = nullptr;

  tempLogPath.clear();
}

/////////////////////////////////////////////////////////////////////////////////////////
void Log::ApplyConfigLogLevel()
{
  // --log-level sets both to the same; being different means --log-level not specified
  if (terminalLogLevel == 3 && fileLogLevel == 1)
  {
    QString level = SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, "logLevel").toString();
    if (level.size())
    {
      int levelValue = ParseLogLevel(level);
      if (levelValue != -1)
      {
        fileLogLevel = levelValue;
      }
    }
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
int Log::ParseLogLevel(const QString& str)
{
  if (str == "debug")     return 0;
  if (str == "info")      return 1;
  if (str == "warn")      return 2;
  if (str == "error")     return 3;
  if (str == "fatal")     return 4;
  return -1;
}

/////////////////////////////////////////////////////////////////////////////////////////
void Log::SetLogLevel(const QString& level)
{
  terminalLogLevel = ParseLogLevel(level);
  fileLogLevel = terminalLogLevel;
}
