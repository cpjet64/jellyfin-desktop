// Exercises the production cached draw-set membership representation.
#include <QtQuick/private/qsgbatchrenderer_p.h>
#include "QtRecoveryPrimitive_p.h"
#include <cassert>
#include <cstdlib>

#undef assert
#define assert(x) do { if (!(x)) std::abort(); } while (false)

using QSGBatchRenderer::DrawSet;

static bool contains(const DrawSet &set, quint64 id)
{
    for (quint64 member : set.recoveryNodes) {
        if (member == id)
            return true;
    }
    return false;
}

int main()
{
    QSGGeometry quad(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4);
    quad.setDrawingMode(QSGGeometry::DrawTriangleStrip);
    auto *points = quad.vertexDataAsTexturedPoint2D();
    points[0].set(0, 0, 0, 0); points[1].set(1, 0, 1, 0);
    points[2].set(0, 1, 0, 1); points[3].set(1, 1, 1, 1);
    assert(qtRecoveryHasDrawablePrimitive(&quad));
    DrawSet first(0, 0, 0), second(0, 0, 10);
    // uploadMergedElement synthesizes indices for this nonindexed quad.
    first.addRecoveryContribution(11, qtRecoveryHasDrawablePrimitive(&quad), 0, 6);
    // An indexed quad contributes its uploaded indices to its own set.
    QSGGeometry indexed(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4, 4);
    indexed.setDrawingMode(QSGGeometry::DrawTriangleStrip);
    auto *indexedPoints = indexed.vertexDataAsTexturedPoint2D();
    for (int i = 0; i < 4; ++i) indexedPoints[i] = points[i];
    auto *indices = indexed.indexDataAsUShort();
    for (quint16 i = 0; i < 4; ++i) indices[i] = i;
    second.addRecoveryContribution(22, qtRecoveryHasDrawablePrimitive(&indexed), 0, 6);
    QSGGeometry empty(QSGGeometry::defaultAttributes_TexturedPoint2D(), 0);
    first.addRecoveryContribution(77, qtRecoveryHasDrawablePrimitive(&empty), 6, 8);
    first.addRecoveryContribution(88, qtRecoveryHasDrawablePrimitive(&quad), 6, 6);
    for (int i = 0; i < 4; ++i) indices[i] = 0;
    assert(!qtRecoveryHasDrawablePrimitive(&indexed));
    first.addRecoveryContribution(99, qtRecoveryHasDrawablePrimitive(&indexed), 6, 8);
    for (int i = 0; i < 4; ++i) points[i].set(float(i), 0, 0, 0);
    assert(!qtRecoveryHasDrawablePrimitive(&quad));
    assert(contains(first, 11) && !contains(first, 22));
    assert(!contains(first, 77) && !contains(first, 88) && !contains(first, 99));
    assert(contains(second, 22) && !contains(second, 11));
    DrawSet cached = second;
    assert(contains(cached, 22) && !cached.recoveryOverflow);
    cached.addRecoveryNode(22); // duplicate does not consume another slot
    cached.addRecoveryNode(33); cached.addRecoveryNode(44); cached.addRecoveryNode(55);
    assert(!cached.recoveryOverflow);
    cached.addRecoveryNode(66);
    assert(cached.recoveryOverflow); // draw-use marker must reject this set
}
