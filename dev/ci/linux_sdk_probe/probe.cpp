#include <QCoreApplication>
#include <QLibraryInfo>
#include <QtGui/private/qguiapplication_p.h>
#include <QtQuick/private/qquickitem_p.h>
#include <QtWebEngineCore/private/qtwebenginecoreglobal_p.h>
#include <GL/gl.h>
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <xkbcommon/xkbcommon.h>
#include <iostream>

extern "C" int mpv_probe_main(int, char**);

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (mpv_probe_main(argc, argv) != 0) return 1;
    std::cout << qVersion() << '\n'
              << QLibraryInfo::path(QLibraryInfo::PrefixPath).toStdString() << '\n';
    return 0;
}
