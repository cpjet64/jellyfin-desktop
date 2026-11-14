# MpvQt hook source-review snapshot

This snapshot is incomplete, unvalidated, and not an integration candidate.
`recoveryFrameObservationAvailable()` always returns false. It requires the
separate RendererOwners implementation and corresponding player integration.

The proposed copied-source patch transfers item and renderer lifetimes to the
owner and provides a bounded generation/request usability exchange. Generation
is published at renderer creation and synchronization before the initial ready
notification; this ordering has not been validated against a running Qt loop.
Context creation or rendering failure produces a typed blocked notification.

Known blockers:

- Normal queued MpvController command, property, observation and event-handler
  calls still use a raw handle. Bind a weak owner and hold a strong native lease
  over every native operation and event drain, including shutdown races.
- The native bridge resolver, selected-frame and source-progress observations,
  and exact Qt submission/swap attribution are absent. No callback or unrelated
  swap establishes a qualifying frame.
- Lost-context quarantine currently occurs without an owned stop request and
  acknowledgement, so audio can continue.
- Adopt-failure fallback queues cleanup on the worker; failed queueing has no
  completion path. Review containment and owner shutdown with player integration.
- Usability requests have no cancellation interface; a nonresponsive render
  thread relies on the controller's deadline.

Qt 6.8's threaded loop emits frameSwapped even after an endFrame error; the
signal does not identify a native render serial. See [Qt source](https://raw.githubusercontent.com/qt/qtdeclarative/6.8/src/quick/scenegraph/qsgthreadedrenderloop.cpp)
and [Qt documentation](https://doc.qt.io/qt-6/qquickwindow.html).

Only patch application and source/hash checks are allocated. No compiler,
CMake configuration, tests, application, fixture, CI, or graphics execution
has been performed.
