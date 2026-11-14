# MpvQt hook source-review snapshot

This snapshot is incomplete, unvalidated, and not an integration candidate.
The capability remains unavailable until a matching private Qt and mpv bridge
produces an actual attempt-zero submission. It requires the separate owner,
Qt adapter, native bridge and corresponding player integration.

The proposed copied-source patch transfers item and renderer lifetimes to the
owner and provides a bounded generation/request usability exchange. Generation
is published at renderer creation and synchronization before the initial ready
notification; this ordering has not been validated against a running Qt loop.
Context creation or rendering failure produces a typed blocked notification.

Known blockers:

- A controller lease patch is applied to the copied source after binding the
  owner on the controller thread. Its coverage and shutdown races require
  independent review and validation; raw handle getters remain borrowed.
- The optional native and Qt bridges propose selected-render and private
  submission records bound by generation, attempt, window and ticket. The
  source and ABI must be integrated and independently reviewed; no matching
  binary or runtime capability has been verified. Public Qt signals only
  trigger a private poll and do not themselves establish successful submission.
- Lost-context quarantine currently occurs without an owned stop request and
  acknowledgement, so audio can continue.
- Adopt-failure fallback queues cleanup on the worker; failed queueing retains
  the allocation without a completion path. Review containment and owner
  shutdown with player integration.
- Usability requests have no cancellation interface; a nonresponsive render
  thread relies on the controller's deadline.

Qt 6.8's threaded loop emits frameSwapped even after an endFrame error; the
signal does not identify a native render serial. See [Qt source](https://raw.githubusercontent.com/qt/qtdeclarative/6.8/src/quick/scenegraph/qsgthreadedrenderloop.cpp)
and [Qt documentation](https://doc.qt.io/qt-6/qquickwindow.html).

Only patch application and source/hash checks are allocated. No compiler,
CMake configuration, tests, application, fixture, CI, or graphics execution
has been performed.
