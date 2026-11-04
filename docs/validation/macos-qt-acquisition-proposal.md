# macOS Qt acquisition boundary

Status: proposal only. No Darwin reader, archive acquisition, extraction, SDK
publication, or SDK readiness has been demonstrated. The target is an Intel
application built with `CMAKE_OSX_ARCHITECTURES=x86_64`; universal Qt archive
names do not establish an ARM or universal application claim.

## Inputs and storage

The existing `dev/ci/qt-6.9.3-unix.json` pins eight macOS archives totaling
362,026,613 compressed bytes. Its 2,014,258,464 expanded bytes are publisher
metadata, not a measured maximum or peak. Before acquisition, determine an
enforced expansion ceiling and available free space on the actual destination
volume. Account simultaneously for verified archives, an extraction stage,
any existing published SDK, build trees, temporary files, and safety headroom.
Do not assume publication removes the need to budget an existing SDK. Stop on
insufficient space; do not infer safety from the publisher estimate.

## Native evidence required before an adapter

1. Identify an already available structured reader by exact canonical path,
   package or operating-system provenance, version, architecture and ABI,
   integrity identity, and supported 7z features. Establish its error and
   metadata semantics, including undecodable names, file types, link targets,
   permissions and bounded streaming. Do not load an unidentified library or
   treat the Ubuntu libarchive identity checks as Darwin evidence.
2. On the actual workspace volume, use reviewed, task-owned fixtures to test
   case and Unicode-name collisions, link creation and observation, and native
   file, directory and symlink mode behavior. Determine a fail-closed collision
   policy for every planned path and its parents before writing archive data.
   Preserve Qt framework link topology and reject escaping or cyclic links.
3. Demonstrate an atomic, same-volume, no-replace directory publication
   operation, including a pre-existing destination and a concurrent contender.
   A prior existence check followed by ordinary rename is not sufficient.
4. Record the filesystem identity, native tool identities, exact fixture
   commands and results. A proposed fixture requires coordinator review and a
   single native lease; no mount, volume or global configuration change is
   implied.

## Proposed implementation boundary

Verify all pinned byte counts and SHA-256 digests before reading entries.
Use a structured first pass to bound member count, metadata and expansion and
validate paths, types, overlays, modes and the complete link graph. Replay the
same headers when streaming regular files into a fresh private stage; reject
any mismatch, unsupported entry, path collision or reader error. Validate
actual written bytes and overlays, create links only after regular entries,
verify materialized metadata, and publish only after all verification succeeds.
Keep native reader selection, collision semantics, link/mode policy and atomic
publication behind a Darwin-specific boundary grounded in the evidence above.

The Linux helpers contain potentially reusable concepts, but their current
implementation is not a portable Darwin contract: `validate_manifest` expects
ten Linux archives, `verify_materialized_link` requires Linux's 0777 symlink
mode, and `publish_stage` explicitly rejects Darwin. Avoid invoking those
native-dependent paths on macOS or silently falling back to shell extraction.
Any shared refactoring requires separate coordination and regression tests.

## SDK readiness evidence

Before claiming SDK readiness, verify the same published prefix provides
Qt 6.9.3 CMake package and required modules, including version-matched
`GuiPrivate` and `QuickPrivate` headers. Verify Intel slices and the dependency
closure of required frameworks and plugins, including the
`QtWebEngineProcess` bundle and its dependencies. A compile and link probe for
the required GUI/private interfaces can precede graphics availability, but does
not prove a graphical session, runtime functionality, packaging, or application
acceptance. Record exact tools, prefix, architecture and outcomes.
