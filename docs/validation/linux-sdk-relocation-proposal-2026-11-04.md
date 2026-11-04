# Linux Qt SDK prerequisite and metadata relocation proposal

**Status: SDK probe blocked; proposal only.** The native prerequisite attempt
did not produce an SDK-ready receipt, desktop build, CI result, or acceptance
evidence. No Qt metadata normalization has been implemented or performed.

## Observed prerequisite progress

A standalone Linux x86-64 attempt at source
`3a2e46a20d4707a61544add409300d2b5dadd654` materialized all ten pinned Qt
archives. The recorded decoded regular-file total was 2,215,335,060 bytes; this
is not a measurement of peak storage. The link inventory recorded 156 verified
links: 148 had archived and native mode `0777`, while eight had archived mode
`0755` and native mode `0777`. The real 7z fixture passed, as did nine native
pure-test suites comprising 60 tests. Four pinned mpv headers were staged.
These results are prerequisite evidence, not a successful application build.

Both `qmake6` and `qtpaths6` reported Qt 6.9.3 and the canonical candidate
prefix. The SDK probe then stopped at `pkg-config --variable=prefix Qt6Core`:
it returned the publisher's build-time prefix `/home/qt/work/install` instead
of the candidate prefix. READY was not written. The subsequent CMake, runtime
and host dependency checks were not reached.

The retained candidate's `Qt6Core.pc` and `Qt6Gui.pc` contain that literal
prefix, and derive their include and library directories from it. With the
observed pkgconf 1.8.1, adding `--define-prefix` made the Core prefix, compiler
flags and library path resolve under the candidate. The same flag relocated
the tested WebEngineQuick metadata and flags. This is limited metadata
evidence, not a compile or link proof.

## Why a global prefix override is unsuitable

The same `--define-prefix` option changed the observed host zlib prefix from
`/usr` to `/usr/lib`, its include directory from `/usr/include` to
`/usr/lib/include`, and its library directory to
`/usr/lib/lib/x86_64-linux-gnu`. Its compiler and linker flags consequently
referenced those incorrect paths. A wrapper that applies the flag globally
would therefore alter host dependencies as well as Qt and is rejected.

The tested job-local mpv input supplies explicit headers and a runtime DSO;
it does not create a job-local `mpv.pc`. A `pkg-config` lookup for mpv in the
observed environment failed, which is not a claim that the explicit mpv input
failed. The proposed Qt metadata treatment does not add or replace mpv input.

## Proposed Qt-only normalization

Subject to separate approval and the inventory below, operate only on eligible
regular `.pc` files in the fresh job-owned Qt candidate's `lib/pkgconfig`
directory. Replace exactly the single complete byte line
`prefix=/home/qt/work/install` with `prefix=${pcfiledir}/../..`, preserving the
line ending, every other byte, and the file's ordinary mode. The relative
expression refers to the candidate prefix from that specific directory. Do not
normalize host metadata or use a global prefix override.

Before any write, enumerate the whole bounded directory and reject symlinks,
path escapes, non-regular files, unsupported modes, oversized files, malformed
or repeated prefix assignments, unexpected prefix values, continuations or
other ambiguous multiline syntax, and any unhandled occurrence of the baked
prefix. Require the exact eligible input set and check all entries before
changing any. If any entry falls outside the established contract, stop and
preserve the candidate rather than broadening the replacement.
Confirm Qt ownership from the pinned archive and an exact reviewed filename
set; location in the candidate alone does not establish ownership. Check
that canonicalized `pcfiledir` and resulting paths stay inside the candidate,
including paths containing spaces and the parser's handling of line endings.

Record each eligible file's relative name, type, mode, byte count and SHA-256
before and after the deterministic transformation. Verify the output differs
only in the approved line and retains the original mode. A bounded read-only
inventory of the retained candidate found 70 distinct `Qt6*.pc` files, all
regular files with mode `0644`. Each had exactly one line matching the baked
prefix and no other line containing that baked path. The
[sanitized inventory](linux-qt-pc-inventory-2026-11-04.json) records each input
hash and the proposed replacement hash; these are computed predictions, not
hashes of rewritten files. The inventory does not establish that every file
has unambiguous multiline syntax, that the candidate still has the same
contents at a later time, or that normalized metadata works in a build.

After an approved implementation on a fresh candidate, verify all required Qt
modules with default `pkg-config`, including version, canonical metadata and
prefix, include flags and library flags. Compare host zlib's default results
before and after and confirm it remains unchanged. Exercise the existing
Qt/mpv CMake and runtime probes at the same canonical candidate path, then
write READY only after every required check passes. Missing modules, remaining
baked paths, unexpected host resolution or any probe failure must block READY.
No CMake or runtime success is asserted by this proposal.

The candidate inventory, exact transformation boundaries, publication and
validation sequence are presented for review. No metadata write, host
configuration change, new package, build, or retry is authorized by this
proposal alone.
