// Private, render-thread scoped native swap observation for Qt 6.9.3.
#include "QtRecoveryAbi.h"

namespace {
struct Scope {
    QtRecoverySwapTicketV1 ticket{};
    bool active = false;
    bool valid = false;
    unsigned reports = 0;
};
thread_local Scope scope;

bool equal(const QtRecoverySwapTicketV1 &a, const QtRecoverySwapTicketV1 &b)
{
    return a.size == b.size && a.version == b.version
        && a.window_lifetime == b.window_lifetime
        && a.scenegraph_generation == b.scenegraph_generation
        && a.frame_serial == b.frame_serial && a.render_thread == b.render_thread
        && a.swapchain == b.swapchain && a.render_target == b.render_target
        && a.surface == b.surface && a.platform_context == b.platform_context;
}

bool validTicket(const QtRecoverySwapTicketV1 *t)
{
    return t && t->size == sizeof(*t) && t->version == QT_RECOVERY_SWAP_ABI_VERSION
        && t->window_lifetime && t->scenegraph_generation && t->frame_serial
        && t->render_thread && t->swapchain && t->render_target
        && t->surface && t->platform_context;
}
}

extern "C" const char *qt_recovery_swap_source_id_v1(void)
{
    return QT_RECOVERY_SWAP_SOURCE_ID;
}

extern "C" int qt_recovery_swap_begin_v1(const QtRecoverySwapTicketV1 *ticket)
{
    if (scope.active) {
        scope.valid = false;
        return 0;
    }
    if (!validTicket(ticket))
        return 0;
    scope = {};
    scope.ticket = *ticket;
    scope.active = true;
    scope.valid = true;
    return 1;
}

extern "C" void qt_recovery_swap_report_v1(uintptr_t surface, uintptr_t context,
                                             int desktopWgl, int doubleBuffered,
                                             int swapSucceeded)
{
    if (!scope.active)
        return;
    ++scope.reports;
    if (scope.reports != 1 || surface != scope.ticket.surface
        || context != scope.ticket.platform_context || desktopWgl != 1
        || doubleBuffered != 1 || swapSucceeded != 1)
        scope.valid = false;
}

extern "C" int qt_recovery_swap_finish_v1(const QtRecoverySwapTicketV1 *ticket,
                                            int qtEndSuccess, int skipPresent)
{
    const bool success = scope.active && scope.valid && validTicket(ticket)
        && equal(scope.ticket, *ticket) && scope.reports == 1
        && qtEndSuccess == 1 && skipPresent == 0;
    scope = {};
    return success ? 1 : 0;
}

extern "C" void qt_recovery_swap_abort_v1(const QtRecoverySwapTicketV1 *)
{
    scope = {};
}
