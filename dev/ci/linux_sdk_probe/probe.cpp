#include <QCoreApplication>
#include <QLibraryInfo>
#include <QtGui/private/qguiapplication_p.h>
#include <QtQuick/private/qquickitem_p.h>
#include <QtWebEngineCore/private/qtwebenginecoreglobal_p.h>
#ifdef PROBE_OPENGL
#include <GL/gl.h>
#else
#include <GLES2/gl2.h>
#endif
#include <xkbcommon/xkbcommon.h>
#include <iostream>

extern "C" int mpv_probe_main(int, char**);

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    auto volatile glSymbol = &glGetString;
    if (!glSymbol) return 1;
    auto* xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkb) return 1;
    xkb_context_unref(xkb);
    if (mpv_probe_main(argc, argv) != 0) return 1;
    std::cout << qVersion() << '\n'
              << QLibraryInfo::path(QLibraryInfo::PrefixPath).toStdString() << '\n';
    return 0;
}
