#include "../scenegraph/coreapi/qsgbatchrenderer_p.h"
#include <QtCore/QDebug>

QT_BEGIN_NAMESPACE
namespace QSGBatchRenderer {
#ifndef QT_NO_DEBUG_OUTPUT
static void dumpVertices(const Batch *b, const QSGGeometry *g, bool depth, int (*typeSize)(int))
{
    const char *vd = b->vbo.data;
    qDebug() << "  -- Vertex Data, count:" << b->vertexCount << " - " << g->sizeOfVertex() << "bytes/vertex";
    for (int i = 0; i < b->vertexCount; ++i) {
        QDebug dump = qDebug().nospace();
        dump << "  --- " << i << ": ";
        int offset = 0;
        for (int a = 0; a < g->attributeCount(); ++a) {
            const QSGGeometry::Attribute &attr = g->attributes()[a];
            dump << attr.position << ":(" << attr.tupleSize << ",";
            if (attr.type == QSGGeometry::FloatType) {
                dump << "float ";
                if (attr.isVertexCoordinate)
                    dump << "* ";
                for (int t = 0; t < attr.tupleSize; ++t)
                    dump << *(const float *)(vd + offset + t * sizeof(float)) << " ";
            } else if (attr.type == QSGGeometry::UnsignedByteType) {
                dump << "ubyte ";
                for (int t = 0; t < attr.tupleSize; ++t)
                    dump << *(const unsigned char *)(vd + offset + t * sizeof(unsigned char)) << " ";
            }
            dump << ") ";
            offset += attr.tupleSize * typeSize(attr.type);
        }
        if (b->merged && depth) {
            float zorder = ((float *)(b->vbo.data + b->vertexCount * g->sizeOfVertex()))[i];
            dump << " Z:(" << zorder << ")";
        }
        vd += g->sizeOfVertex();
    }
}

template<typename T> static void dumpIndices(const Batch *b)
{
    const T *id = (const T *) b->ibo.data;
    QDebug dump = qDebug();
    dump << "  -- Index Data, count:" << b->indexCount;
    for (int i = 0; i < b->indexCount; ++i) {
        if ((i % 24) == 0)
            dump << Qt::endl << "  --- ";
        dump << id[i];
    }
}

void qsgRecoveryDumpUpload(const Batch *b, const QSGGeometry *g, bool depth, bool uint32Indices,
                           int (*typeSize)(int))
{
    dumpVertices(b, g, depth, typeSize);
    if (b->drawSets.isEmpty())
        return;
    if (uint32Indices)
        dumpIndices<quint32>(b);
    else
        dumpIndices<quint16>(b);
    for (int i = 0; i < b->drawSets.size(); ++i) {
        const DrawSet &s = b->drawSets.at(i);
        qDebug() << "  -- DrawSet: indexCount:" << s.indexCount << " vertices:" << s.vertices
                 << " z:" << s.zorders << " indices:" << s.indices;
    }
}
#endif
}
QT_END_NAMESPACE
