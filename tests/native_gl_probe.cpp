#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>

#include "windows_desktop_metadata.h"

static QString glString(QOpenGLFunctions* functions, GLenum name)
{
  const auto* value = functions->glGetString(name);
  return value ? QString::fromLatin1(reinterpret_cast<const char*>(value)) : QString();
}

static QString describeFormat(const QSurfaceFormat& format)
{
  return QStringLiteral("%1.%2 profile=%3 renderable=%4")
    .arg(format.majorVersion()).arg(format.minorVersion()).arg(format.profile()).arg(format.renderableType());
}

static QJsonObject probe()
{
  QJsonObject result{{"desktop", windowsDesktopMetadata()}, {"platform", QGuiApplication::platformName()}};
  QOpenGLContext context;
  context.setFormat(QSurfaceFormat::defaultFormat());
  const bool created = context.create();
  result["contextCreated"] = created;
  result["contextValid"] = context.isValid();
  if (!created || !context.isValid()) return result;
  result["contextFormat"] = describeFormat(context.format());
  QOffscreenSurface surface;
  surface.setFormat(context.format());
  surface.create();
  result["surfaceValid"] = surface.isValid();
  if (!surface.isValid()) return result;
  result["surfaceFormat"] = describeFormat(surface.format());
  const bool current = context.makeCurrent(&surface);
  result["makeCurrent"] = current;
  if (current) {
    auto* functions = context.functions();
    result["vendor"] = glString(functions, GL_VENDOR);
    result["renderer"] = glString(functions, GL_RENDERER);
    result["version"] = glString(functions, GL_VERSION);
    context.doneCurrent();
  }
  return result;
}

int main(int argc, char** argv)
{
  QGuiApplication app(argc, argv);
  if (argc != 2 || !QFileInfo(QString::fromLocal8Bit(argv[1])).isAbsolute()) return 2;
  const QJsonObject result = probe();
  QFile output(QString::fromLocal8Bit(argv[1]));
  if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 3;
  if (output.write(QJsonDocument(result).toJson()) < 0) return 3;
  return result.value("makeCurrent").toBool() ? 0 : 1;
}
