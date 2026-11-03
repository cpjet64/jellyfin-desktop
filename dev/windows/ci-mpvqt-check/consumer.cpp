#include <MpvController>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QObject>
#include <QTemporaryDir>
#include "mpv_test_bootstrap.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    QTemporaryDir scratch(QDir(QString::fromLocal8Bit(argv[1])).filePath("mpvqt-consumer-XXXXXX"));
    QString error;
    if (!scratch.isValid() || !MpvTestBootstrap::prepareMpvTestEnvironment(scratch.path(), &error)) {
        qCritical().noquote() << "MpvQt consumer bootstrap failed:" << error;
        return 1;
    }
    QObject* object = new MpvController;
    delete object;
    return 0;
}
