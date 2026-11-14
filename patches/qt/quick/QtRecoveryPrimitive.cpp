#include "QtRecoveryPrimitive_p.h"
#include <cmath>

QT_BEGIN_NAMESPACE
namespace {
bool standardTextureGeometry(const QSGGeometry *g)
{
    if (!g || g->vertexCount() < 3 || g->vertexCount() > 65534 || g->indexCount() > 65534
        || g->indexType() != QSGGeometry::UnsignedShortType || g->attributeCount() != 2
        || g->sizeOfVertex() != int(sizeof(QSGGeometry::TexturedPoint2D)))
        return false;
    const auto *a = g->attributes();
    return a[0].position == 0 && a[0].type == QSGGeometry::FloatType && a[0].tupleSize == 2
            && a[1].position == 1 && a[1].type == QSGGeometry::FloatType && a[1].tupleSize == 2;
}

bool nondegenerate(const QSGGeometry::TexturedPoint2D *p, unsigned a, unsigned b, unsigned c)
{
    if (a == b || b == c || a == c)
        return false;
    const double area = (double(p[b].x) - p[a].x) * (double(p[c].y) - p[a].y)
            - (double(p[b].y) - p[a].y) * (double(p[c].x) - p[a].x);
    return std::isfinite(area) && area != 0.0;
}
}

bool qtRecoveryHasDrawablePrimitive(const QSGGeometry *g)
{
    if (!standardTextureGeometry(g))
        return false;
    const unsigned mode = g->drawingMode();
    if (mode != QSGGeometry::DrawTriangles && mode != QSGGeometry::DrawTriangleStrip)
        return false;
    const int count = g->indexCount() ? g->indexCount() : g->vertexCount();
    if (count < 3)
        return false;
    const quint16 *indices = g->indexCount() ? g->indexDataAsUShort() : nullptr;
    for (int i = 0; indices && i < count; ++i) {
        if (indices[i] >= g->vertexCount())
            return false;
    }
    const auto *vertices = g->vertexDataAsTexturedPoint2D();
    const int step = mode == QSGGeometry::DrawTriangles ? 3 : 1;
    for (int i = 0; i + 2 < count; i += step) {
        const unsigned a = indices ? indices[i] : unsigned(i);
        const unsigned b = indices ? indices[i + 1] : unsigned(i + 1);
        const unsigned c = indices ? indices[i + 2] : unsigned(i + 2);
        if (nondegenerate(vertices, a, b, c))
            return true;
    }
    return false;
}
QT_END_NAMESPACE
