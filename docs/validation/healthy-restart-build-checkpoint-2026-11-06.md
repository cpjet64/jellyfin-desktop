# Healthy restart build checkpoint — 2026-11-06

The Windows Debug healthy restart diagnostic build at
`ddf74a840e3a55e13a75bc2b5596fe5798c110e2` configured and built successfully.
Its five registered headless tests passed. The held `test_healthy_restart`
executable compiled and linked but was not executed; its SHA-256 is
`01c0dcc490c9d887157893c67560596ac169cc37d31f20cc166ca5c14cf8db2a`.
Its current main returns 78: compilation and linking do not demonstrate an
actual A/B restart.

The configuration used Ninja Debug, static MpvQt and external events ON,
Player intent and healthy restart diagnostic options ON, and C2 and the older
fixture-copy option OFF. The build included the held target, five headless
targets and two additional build-only targets. The headless invocation was
`ctest --test-dir <build> -R "^healthy_(mpvqt_event_ownership|player_native_receipts|player_restore_cleanup|player_restore_observation|player_native_restore)$" --no-tests=error --output-on-failure --timeout 60`.
The retained W864 proof includes the exact configure and build arguments,
results, Ninja commands and dependency records, compile database and autogen
metadata. The build log SHA-256 is
`054bbc80ef06f224dacd1dcb38e69c6ea0c148b3d37998c1090a43acc10d73aa`.

An independent Windows graph review covered 55 active compile entries and
their dependency records. They use external events without the C2 option,
contain no original Player or MpvQt source dependencies, and use the copied
Player header for MOC. The held target links the healthy core and healthy
MpvQt libraries. The review is limited to this Windows Debug graph; it does
not establish native teardown or callback-barrier behavior.

Earlier attempts exposed and resolved three build issues: W848's generated
wrapper used Windows line endings inconsistent with its hash; W858 generated
duplicate MOC definitions; W860 lacked the copied include interface. Those
failed attempts were retained separately. At `0906bec`, the earlier default
Windows checks recorded 54 and 51 tests, four copy checks and nine older
checks; the later copy acceptance had seven checks.

Linux and macOS native graph validation and CI remain pending. Actual render
retirement is not integrated, and the native barrier remains unqualified. No
native A/B recovery, real media replacement or held fixture execution is
claimed by this checkpoint.
