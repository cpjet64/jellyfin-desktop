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
installed. The source candidate adds ticket, FBO, draw-recording and private
completion hooks, but has not been compiled, tested, or independently reviewed.
It does not establish runtime capability or a successful frame submission.

Apply the `qtdeclarative-6.9.3-ticket-fbo.patch` and
`qtdeclarative-6.9.3-batch-renderer.patch` files with `git apply --unidiff-zero` at the pinned
Qt Declarative root, then place all adjacent `QtRecovery*.h` and
`QtRecovery*.cpp` files in `src/quick/recovery/`. Copy
`patches/qt/quick/tests/CMakeLists.txt` and
`tst_qtrecovery_aggregate.cpp`, and `tst_qtrecovery_drawsets.cpp` into
`tests/auto/quick/qtrecoveryaggregate/`.
The patched Qt Base private bridge must be built and installed as part of the
same compatible kit. The registered source-only tests have not been executed.
The source apply check passed for both patches together. Their SHA-256 values
for the current LF bytes are:

| Patch | SHA-256 |
| --- | --- |
| `qtdeclarative-6.9.3-ticket-fbo.patch` | `efb8ae00aa96ec78f9c618b7e7498b072417014d3ad004b88d9107fbf2df0b45` |
| `qtdeclarative-6.9.3-batch-renderer.patch` | `18f64d8b67e702cd9407f49776bd982f0152f616f34b34290a22be03e85e8acd` |

The threaded loop begins the frame before synchronization and calls `endFrame`
after rendering. A failed begin can still emit `afterFrameEnd`; a failed end can
still be followed by `fireFrameSwapped`. Neither signal is submission proof.
The FBO node performs its optional multisample resolve before scene rendering;
the resolved texture, when present, is the display texture. The batch renderer
can retain uploaded batches across frames, so merged node membership must be
retained with the corresponding draw set and invalidated with its upload data.
The candidate retains bounded node identities with each uploaded merged draw
set and records nonempty draw calls against the expected render target. It
checks the standard texture material's selected QSG and QRhi texture bindings
against the registered display texture and retains attributed content across
tickets. Multisample resolves fail closed: the upstream resolve operation
returns no result from which this adapter can establish successful transfer.
The helper test exercises the actual Quick aggregation and QtGui swap bridge
with synthetic identities, including zero-attempt readiness, failed swaps and
retained-frame reuse. A separate source test exercises the batch renderer's
draw-set membership storage and overflow. The actual renderer draw and
material-binding integration remain untested; integration review remains open.

The publication boundary rejects out-of-range identity and timestamp values
before retaining a native frame. The added negative source cases have not been
run. Upload counting, merged/unmerged copies, and diagnostics now have separate
cohesive routines; uniform and sampler updates have separate routines as well.
`uploadBatch` shrinks from 248 to 89 physical lines, and
`updateMaterialDynamicData` from 202 to 99. The latter contains 75 nonblank
lines excluding line comments, a conservative line-count proxy, not a measured
logical-line count. The existing renderer source shrinks from 4,324 to 4,259
physical lines and its private header from 1,030 to 997. These inherited files
remain oversized; the extraction and its behavior need independent review.
