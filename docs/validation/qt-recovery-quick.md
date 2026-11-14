# Qt Quick recovery attribution source identity

The selected upstream source is Qt Declarative `v6.9.3`, annotated tag object
`f280b2401205fc13f18ad4b195195b83fd52224f`, peeled commit
`c1640ba76059882e1d2bc978b6904a19333be4a9`. The following SHA-256 values
identify the individual unmodified source files retrieved from that commit.

| Source path | SHA-256 |
| --- | --- |
| `src/quick/items/qquickframebufferobject.cpp` | `efe98b5fa43b230e6cfa91603b8691306464ecd3d6c455daf76e6b5a7e55478e` |
| `src/quick/scenegraph/qsgthreadedrenderloop.cpp` | `261608adaf6b5d4890892d03aa9c7a35ee48b1f74ac329d180c9e430b546e5c6` |
| `src/quick/scenegraph/coreapi/qsgbatchrenderer.cpp` | `969582a59669f39d30ed0cacbcdd4d0a169144b3a9ab18f83cc084380fc159cc` |
| `src/quick/scenegraph/coreapi/qsgbatchrenderer_p.h` | `e45f68c55591f2f5365424cbd0b2b947ba2cb59cc4ad228d9e4ac7f4590c9d4c` |

This is a source identity record, not evidence that a patched Qt was built or
installed. The source slice adds ticket, FBO and draw-recording hooks, but
complete draw-use proof and its negative tests remain outstanding. Submission
consumption always fails closed; these files do not establish capability.

Apply `patches/qt/quick/qtdeclarative-6.9.3-ticket-fbo.patch` with
`git apply --unidiff-zero` at the pinned
Qt Declarative root, then place the three adjacent `QtRecovery*.{h,cpp}` files
in `src/quick/recovery/`. The patched Qt Base private bridge must be built and
installed as part of the same compatible kit. The source-only test is not yet
integrated into the upstream Qt test build and has not been executed.

The threaded loop begins the frame before synchronization and calls `endFrame`
after rendering. A failed begin can still emit `afterFrameEnd`; a failed end can
still be followed by `fireFrameSwapped`. Neither signal is submission proof.
The FBO node performs its optional multisample resolve before scene rendering;
the resolved texture, when present, is the display texture. The batch renderer
can retain uploaded batches across frames, so merged node membership must be
retained with the corresponding draw set and invalidated with its upload data.
The current source retains bounded node identities with each uploaded merged
draw set and records nonempty draw calls against the expected render target.
This is not yet sufficient to establish that the active material samples the
registered texture, or that effect and layer paths are excluded. It also does
not yet retain a valid paused frame across new tickets. These gaps must be
closed before enabling completion.
