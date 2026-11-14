#include "../scenegraph/coreapi/qsgbatchrenderer_p.h"
#include <cstring>
#if defined(Q_OS_WIN) && QT_CONFIG(opengl)
#include "QtRecoveryQuick_p.h"
#include "QtRecoveryPrimitive_p.h"
#endif

QT_BEGIN_NAMESPACE
namespace QSGBatchRenderer {
void Renderer::uploadRecoveryMergedBatch(Batch *b, QSGGeometry *g)
{
    char *vertexData = b->vbo.data;
    char *zData = vertexData + b->vertexCount * g->sizeOfVertex();
    char *indexData = b->ibo.data;

    quint16 iOffset16 = 0;
    quint32 iOffset32 = 0;
    Element *e = b->first;
    uint verticesInSet = 0;
    // Start a new set already after 65534 vertices because 0xFFFF may be
    // used for an always-on primitive restart with some apis (adapt for
    // uint32 indices as appropriate).
    const uint verticesInSetLimit = m_uint32IndexForRhi ? 0xfffffffe : 0xfffe;
    int indicesInSet = 0;
    b->drawSets.reset();
    int drawSetIndices = 0;
    const char *indexBase = b->ibo.data;
    b->drawSets << DrawSet(0, zData - vertexData, drawSetIndices);
    while (e) {
        verticesInSet += e->node->geometry()->vertexCount();
        if (verticesInSet > verticesInSetLimit) {
            b->drawSets.last().indexCount = indicesInSet;
            if (g->drawingMode() == QSGGeometry::DrawTriangleStrip) {
                b->drawSets.last().indices += 1 * mergedIndexElemSize();
                b->drawSets.last().indexCount -= 2;
            }
            drawSetIndices = indexData - indexBase;
            b->drawSets << DrawSet(vertexData - b->vbo.data,
                                   zData - b->vbo.data,
                                   drawSetIndices);
            iOffset16 = 0;
            iOffset32 = 0;
            verticesInSet = e->node->geometry()->vertexCount();
            indicesInSet = 0;
        }
        void *iBasePtr = &iOffset16;
        if (m_uint32IndexForRhi)
            iBasePtr = &iOffset32;
#if defined(Q_OS_WIN) && QT_CONFIG(opengl)
        const int recoveryIndicesBefore = indicesInSet;
#endif
        uploadMergedElement(e, b->positionAttribute, &vertexData, &zData, &indexData, iBasePtr, &indicesInSet);
#if defined(Q_OS_WIN) && QT_CONFIG(opengl)
        b->drawSets.last().addRecoveryContribution(qtRecoveryNodeIdentity(e->node),
                                                   qtRecoveryHasDrawablePrimitive(e->node->geometry()),
                                                   recoveryIndicesBefore, indicesInSet);
#endif
        e = e->nextInBatch;
    }
    b->drawSets.last().indexCount = indicesInSet;
    // We skip the very first and very last degenerate triangles since they aren't needed
    // and the first one would reverse the vertex ordering of the merged strips.
    if (g->drawingMode() == QSGGeometry::DrawTriangleStrip) {
        b->drawSets.last().indices += 1 * mergedIndexElemSize();
        b->drawSets.last().indexCount -= 2;
    }
}

void Renderer::uploadRecoveryUnmergedBatch(Batch *b)
{
    char *vboData = b->vbo.data;
    char *iboData = b->ibo.data;
    Element *e = b->first;
    while (e) {
        QSGGeometry *g = e->node->geometry();
        int vbs = g->vertexCount() * g->sizeOfVertex();
        memcpy(vboData, g->vertexData(), vbs);
        vboData = vboData + vbs;
        const int indexCount = g->indexCount();
        if (indexCount) {
            const int effectiveIndexSize = m_uint32IndexForRhi ? sizeof(quint32) : g->sizeOfIndex();
            const int ibs = indexCount * effectiveIndexSize;
            if (g->sizeOfIndex() == effectiveIndexSize) {
                memcpy(iboData, g->indexData(), ibs);
            } else {
                if (g->sizeOfIndex() == sizeof(quint16) && effectiveIndexSize == sizeof(quint32)) {
                    quint16 *src = g->indexDataAsUShort();
                    quint32 *dst = (quint32 *) iboData;
                    for (int i = 0; i < indexCount; ++i)
                        dst[i] = src[i];
                } else {
                    Q_ASSERT_X(false, "uploadBatch (unmerged)", "uint index with ushort effective index - cannot happen");
                }
            }
            iboData += ibs;
        }
        e = e->nextInBatch;
    }
}
}
QT_END_NAMESPACE
