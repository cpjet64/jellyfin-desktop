# Linux SDK native prerequisite checkpoint — 2026-11-04

**Status: blocked during Qt archive planning.** This checkpoint is not a Linux
desktop build, SDK readiness, CI, release, or acceptance result. The native
checks below ran on Linux x86_64 using isolated job-owned working directories.

## Fixture and source checkpoints

At source `7d80e8e13bb0af1a3d7cc5ef3d794ffaf2ffed23`, a real 7z fixture
stopped during planning because a directory member was named `payload/` and
the path validator rejected its trailing separator. The generated fixture's
SHA-256 was `dd3edfdf62e7646f81db7141427b8113ef6629526547ef92fa161bd9aa204b1c`.
The fixture worker failed; this was not an SDK test pass.

The directory-entry correction was integrated at
`c12313b99693e5d6ee94a8550cf2801dc8f00ce4`. On that source, a newly generated
real 7z fixture passed the structured-reader, planning, replay, and extraction
checks. The check verified file content, an internal relative symlink and an
executable permission bit. Its SHA-256 was
`1ae1add2b6ad14027c052fa375c1337309ad1c0710ca7ca4c9a6ffb8a437e60e`.
These independently generated fixtures have different identities; the second
result does not rewrite the first failure.

## Portable suites on Linux

The same corrected source passed nine suites, 56 tests total:

| Suite | Tests |
| --- | ---: |
| `test_qt_sdk_archive` | 10 |
| `test_linux_qt_plan` | 10 |
| `test_libarchive_reader_contract` | 12 |
| `test_qt_unix_manifest` | 2 |
| `test_linux_sdk_order` | 7 |
| `test_linux_sdk_inputs` | 7 |
| `test_linux_sdk_cmake` | 1 |
| `test_explicit_mpv` | 6 |
| `test_explicit_mpv_cmake` | 1 |

These suite results and the fixture exercise do not establish that the full
Qt SDK can be prepared or used by the application.

## Full Qt archive attempt

The ten-archive acquisition reached the pre-write archive plan and failed
closed on a symlink permission check for `libQt6WebEngineWidgets.so`:
`unsupported symlink permissions`. A subsequent bounded, read-only metadata
probe verified all ten pinned archive identities and read 11,662 headers. It
observed 148 symlinks with mode `0777` and eight with mode `0755` (raw mode
`0120755`). All eight were WebEngine links in archive 09. The rejected link
targets `libQt6WebEngineWidgets.so.6`. The sanitized metadata is recorded in
[linux-sdk-native-2026-11-04-metadata.json](linux-sdk-native-2026-11-04-metadata.json).

The acquisition worker exited nonzero. No Qt SDK ready
receipt, mpv acquisition for this attempt, SDK probe, full application build,
or CI run followed that failure. The metadata probe did not establish SDK
readiness or change the archive policy; any policy decision remains separate.
