# macOS Qt reader probe proposal

Status: proposal only. The W403 receipt records an x86_64 Darwin host, an
absent `/usr/lib/libarchive.dylib` filesystem entry and an absent selected SDK
`usr/include/archive.h`. It does not establish that a reader is unavailable.
Apple describes system libraries residing in the dyld shared cache and warns
that its implementation changes: [Apple DTS explanation](https://developer.apple.com/forums/thread/842864).
Do not encode a cache location or assume an absent file proves an absent image.

## First probe: metadata only

Before any guest execution, review the exact commands and obtain the native
lease. Capture exit status and bounded stdout/stderr for these steps:

1. Record `sw_vers`, `uname -m`, `xcode-select -p`, and
   `xcrun --sdk macosx --show-sdk-path` and `--show-sdk-version`. Confirm the
   selected SDK is the one inspected; record its resolved path.
2. Inspect the existing Apple tool with `otool -L /usr/bin/tar`,
   `codesign -dv --verbose=2 /usr/bin/tar`, and
   `pkgutil --file-info /usr/bin/tar`. These describe a candidate dependency
   and tool provenance; linkage does not prove the candidate reader's identity,
   ABI, exports or 7z capability.
3. List only immediate names in the selected SDK's `usr/lib` and `usr/include`
   directories that start with `libarchive` and `archive`, respectively. Do
   not recurse or inspect arbitrary paths. Review the resulting exact paths
   before inspecting any candidate SDK stub or header. Record missing or
   inaccessible directories and command failures rather than assuming absence.
4. Locate `dyld_shared_cache_util` with `xcrun --find`, then inspect its help or
   manual. Separately review an exact supported metadata-listing command before
   using it. If it can report the candidate install name, architecture and
   exports without extracting or loading an image, record those results with
   the OS build and tool identity. Otherwise stop; do not guess cache paths,
   extract cache contents or load an unidentified library.

The result must bind candidate provenance, install name, architecture, ABI and
required structured-reader symbols to evidence. A pathname, a linked tool, or
an SDK stub alone is insufficient to establish the complete runtime identity.
If this cannot be established, report the gap rather than trying a fallback.

## Separate later decisions

After identity and native execution are approved, a small x86_64 fixture may
test registration, seekable input, entry metadata, bounded payload reads,
truncation and error reporting against known-hash 7z fixtures with representative
compression methods and link types. The upstream [API header](https://github.com/libarchive/libarchive/blob/master/libarchive/archive.h)
declares structured reading and 7z registration; its [examples](https://github.com/libarchive/libarchive/wiki/Examples)
explain that 7z needs seeking. Symbol or registration success does not prove
support for the methods or metadata in the pinned Qt archives. Those verified
archives would require their own later compatibility evidence.

If the OS reader cannot be qualified, a pinned, verified job-local reader build
using the observed x86_64 toolchain is an alternative for separate evaluation.
Select and verify exact upstream source and dependency identities, build options,
license and storage budget first; prove architecture, ABI, 7z methods and
the same fixture contract before using it. This proposal authorizes neither a
download nor a build, native adapter, Qt acquisition or publication.
