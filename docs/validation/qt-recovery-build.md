# Private Qt recovery build boundary

This source is an unexecuted candidate, not a capability or artifact claim. Build
QtBase and QtDeclarative from their pinned matching 6.9.3 source and the same
MSVC kit and configuration as the application's QtWebEngine. Do not mix MinGW
Qt components. Check the source blobs in `patches/qt/base/source-identity.txt`,
apply `qtbase-6.9.3-swap.patch`, and copy `QtRecoverySwap.cpp` and
`QtRecoveryAbi.h` into QtBase `src/gui/recovery/`. The Gui module's private
header filter makes syncqt publish `QtRecoveryAbi.h` under the versioned
`include/QtGui/6.9.3/QtGui/private/` directory. Consumers link Qt::GuiPrivate
and include `<QtGui/private/QtRecoveryAbi.h>`. Copy the test source and
`QtRecoverySwapTests.CMakeLists.txt` as `CMakeLists.txt` into
`tests/auto/gui/kernel/qtrecoveryswap/`. The kernel test registration adds this
dedicated test to Qt's test tree. Verify installed path and test registration in
the later authorized build; neither has been executed here. Build identities and hashes of
all resulting QtGui, QtQuick, and Windows platform plugin binaries must be
recorded and matched by the consumer before enabling this capability.

The C ABI is versioned and size checked. A caller opens a thread-local ticket
only after successful beginFrame, with retained lifetime and generation identities,
and closes or aborts it on every path. The platform hook reports the actual call's
BOOL for a desktop OpenGL double-buffered window. Missing, repeated, mismatched,
failed, skipped, or unsupported swaps fail. QtQuick must separately establish
native frame, resolved display texture and main-pass draw attribution and combine
that evidence with this result. The owner must revoke delivery on destruction
and recheck recovery authority and deadline. This bridge is not GPU completion
or visible-picture evidence. Non-Windows and unpatched builds remain unavailable.

The scope is thread-local, so only the render thread that opened it can observe
and consume a report. An overlapping begin invalidates that scope. The caller
must abort on all early exits and teardown on that same thread; the ticket fields
are opaque identities and this bridge does not itself retain or revoke a window,
context, or observer. The Windows hook compares the actual platform context and
surface, checks the current WGL context and DC, queries the DC pixel format for
a window double buffer, and records the swap's returned BOOL. Any unavailable
observation fails closed. Destruction and cross-thread ownership need matching
QtQuick and owner integration; this source alone does not establish those fences.

No configure, compiler, execution, packaging, or runtime validation is authorized
or claimed here. The later build must validate exports, exact ABI sizes, source
identity, same-kit binary closure, negative cases, and packaged visible playback.
