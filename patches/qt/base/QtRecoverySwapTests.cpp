// Source-only negative cases for the private bridge; not executed in this allocation.
#include "QtRecoveryAbi.h"
#include <cassert>

static QtRecoverySwapTicketV1 ticket()
{
    return {sizeof(QtRecoverySwapTicketV1), QT_RECOVERY_SWAP_ABI_VERSION,
            1, 2, 3, 4, 5, 6, 7, 8};
}

static void report(const QtRecoverySwapTicketV1 &t, int success = 1)
{
    qt_recovery_swap_report_v1(t.surface, t.platform_context, 1, 1, success);
}

int main()
{
    auto t = ticket();
    assert(qt_recovery_swap_begin_v1(&t));
    assert(!qt_recovery_swap_finish_v1(&t, 1, 0)); // missing
    assert(qt_recovery_swap_begin_v1(&t));
    report(t, 0);
    assert(!qt_recovery_swap_finish_v1(&t, 1, 0));
    assert(qt_recovery_swap_begin_v1(&t));
    report(t); report(t);
    assert(!qt_recovery_swap_finish_v1(&t, 1, 0));
    assert(qt_recovery_swap_begin_v1(&t));
    qt_recovery_swap_report_v1(t.surface + 1, t.platform_context, 1, 1, 1);
    assert(!qt_recovery_swap_finish_v1(&t, 1, 0));
    assert(qt_recovery_swap_begin_v1(&t));
    qt_recovery_swap_report_v1(t.surface, t.platform_context + 1, 1, 1, 1);
    assert(!qt_recovery_swap_finish_v1(&t, 1, 0));
    assert(qt_recovery_swap_begin_v1(&t));
    qt_recovery_swap_report_v1(t.surface, t.platform_context, 1, 0, 1);
    assert(!qt_recovery_swap_finish_v1(&t, 1, 0));
    assert(qt_recovery_swap_begin_v1(&t));
    qt_recovery_swap_report_v1(t.surface, t.platform_context, 0, 1, 1);
    assert(!qt_recovery_swap_finish_v1(&t, 1, 0));
    assert(qt_recovery_swap_begin_v1(&t)); report(t);
    assert(!qt_recovery_swap_finish_v1(&t, 0, 0));
    assert(qt_recovery_swap_begin_v1(&t)); report(t);
    assert(!qt_recovery_swap_finish_v1(&t, 1, 1));
    assert(qt_recovery_swap_begin_v1(&t)); report(t);
    auto other = t; ++other.window_lifetime;
    assert(!qt_recovery_swap_finish_v1(&other, 1, 0));
    assert(qt_recovery_swap_begin_v1(&t)); report(t);
    qt_recovery_swap_abort_v1(&t); // destruction or cancellation
    assert(!qt_recovery_swap_finish_v1(&t, 1, 0));
    assert(qt_recovery_swap_begin_v1(&t)); report(t);
    assert(qt_recovery_swap_finish_v1(&t, 1, 0));
}
