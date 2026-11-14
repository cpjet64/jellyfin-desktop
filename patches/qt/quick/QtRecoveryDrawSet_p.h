#ifndef QT_RECOVERY_DRAW_SET_P_H
#define QT_RECOVERY_DRAW_SET_P_H
#include <QtCore/qglobal.h>
QT_BEGIN_NAMESPACE
namespace QSGBatchRenderer {
struct DrawSet
{
    DrawSet(int v, int z, int i)
        : vertices(v)
        , zorders(z)
        , indices(i)
    {
    }
    DrawSet() {}
    int vertices = 0;
    int zorders = 0;
    int indices = 0;
    int indexCount = 0;
#if defined(Q_OS_WIN) && QT_CONFIG(opengl)
    quint64 recoveryNodes[4] = {};
    bool recoveryOverflow = false;
    void addRecoveryContribution(quint64 node, int vertices, int before, int after)
    {
        if (vertices > 0 && after > before)
            addRecoveryNode(node);
    }
    void addRecoveryNode(quint64 node)
    {
        if (!node)
            return;
        for (auto &entry : recoveryNodes) {
            if (entry == node)
                return;
            if (!entry) {
                entry = node;
                return;
            }
        }
        recoveryOverflow = true;
    }
#endif
};
}
QT_END_NAMESPACE
#endif
