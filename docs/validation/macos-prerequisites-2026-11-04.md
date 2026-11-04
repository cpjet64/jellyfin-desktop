# macOS prerequisite observation — 2026-11-04

A bounded read-only metadata probe completed at **09:44:54 UTC** on the native
Intel macOS runner. It reported Darwin `25.6.0`, x86_64, and settled process
groups for the commands it invoked. The reviewed probe's SHA-256 was
`a86ff0e16f21dc052cdf542530128da06aa50b1cd83a876be0e7c784ca7aec9c`;
the retained private result's SHA-256 was
`277f728b1d450ce5758f8fe3e26680c76f1c7087c57eb5f23902ae1405e89199`.
The [selected metadata](macos-prerequisites-2026-11-04.json) excludes host and
account identities and private paths.

The selected PATH supplied CMake 4.4.3, Ninja 1.13.2 and pkg-config 3.0.6.
The direct, guarded C compiler reported Apple clang 21.0.0. The direct C++
compiler check did not report a binary under its non-symlink guard; this does
not prove that no C++ compiler exists. Meson, qmake6 and qtpaths6 were not found
on the selected PATH.

All 14 named Qt, mpv, FFmpeg, libass and libplacebo pkg-config lookups failed.
The selected standard prefixes did not contain the checked Qt frameworks and
CMake packages, mpv headers or library, or MpvQt CMake package. These are
bounded search results, not proof that the software is absent from the host.
The probe did not run Homebrew, install software, change accounts or create a
GUI session.

The existing GUI-session query returned exit 125 and the console owner was UID
0, so the authorized account's existing GUI session and console ownership were
not established. An available native build and pure nonvisual tests can be
assessed separately from GUI availability once their dependencies exist. Native
graphics, runtime and desktop acceptance remain unproven; this observation is
not a green CI result or a product acceptance claim.
