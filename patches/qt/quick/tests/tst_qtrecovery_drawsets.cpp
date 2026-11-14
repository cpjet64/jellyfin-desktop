// Exercises the production cached draw-set membership representation.
#include <QtQuick/private/qsgbatchrenderer_p.h>
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
    DrawSet first(0, 0, 0), second(0, 0, 10);
    // uploadMergedElement synthesizes indices for this nonindexed quad.
    first.addRecoveryContribution(11, 4, 0, 6);
    // An indexed quad contributes its uploaded indices to its own set.
    second.addRecoveryContribution(22, 4, 0, 6);
    first.addRecoveryContribution(77, 0, 6, 6);
    first.addRecoveryContribution(88, 4, 6, 6);
    assert(contains(first, 11) && !contains(first, 22));
    assert(!contains(first, 77) && !contains(first, 88));
    assert(contains(second, 22) && !contains(second, 11));
    DrawSet cached = second;
    assert(contains(cached, 22) && !cached.recoveryOverflow);
    cached.addRecoveryNode(22); // duplicate does not consume another slot
    cached.addRecoveryNode(33); cached.addRecoveryNode(44); cached.addRecoveryNode(55);
    assert(!cached.recoveryOverflow);
    cached.addRecoveryNode(66);
    assert(cached.recoveryOverflow); // draw-use marker must reject this set
}
