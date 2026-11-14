#ifndef QT_RECOVERY_CLIP_STATE_P_H
#define QT_RECOVERY_CLIP_STATE_P_H
#include <private/qdatabuffer_p.h>
#include <rhi/qrhi.h>
QT_BEGIN_NAMESPACE
class QSGClipNode;
namespace QSGBatchRenderer {
struct ClipState
{
    enum ClipTypeBit
    {
        NoClip = 0x00,
        ScissorClip = 0x01,
        StencilClip = 0x02
    };
    Q_DECLARE_FLAGS(ClipType, ClipTypeBit)

    const QSGClipNode *clipList;
    ClipType type;
    QRhiScissor scissor;
    int stencilRef;

    inline void reset();
};

struct StencilClipState
{
    StencilClipState() : drawCalls(1) { }

    bool updateStencilBuffer = false;
    QRhiShaderResourceBindings *srb = nullptr;
    QRhiBuffer *vbuf = nullptr;
    QRhiBuffer *ibuf = nullptr;
    QRhiBuffer *ubuf = nullptr;

    struct StencilDrawCall {
        int stencilRef;
        int vertexCount;
        int indexCount;
        QRhiCommandBuffer::IndexFormat indexFormat;
        quint32 vbufOffset;
        quint32 ibufOffset;
        quint32 ubufOffset;
    };
    QDataBuffer<StencilDrawCall> drawCalls;

    inline void reset();
};
}
QT_END_NAMESPACE
#endif
