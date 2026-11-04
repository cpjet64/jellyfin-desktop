# Linux dependency inventory: corrected read-only result

On 4 November 2026, one corrected read-only inspection completed against the
retained Linux Qt candidate. The inspected harness SHA-256 was
`9b16c7792c7645ffe14c68579cff5c47f8b225f0d55c3e517b3b2163b484ab19`;
the retained receipt SHA-256 was
`ccd47a1da607a0bda215c9e2e125e762d5631701e9747c74e25c783fb86add8e`.
The process exited 0 without truncated output. This is separate from the
[earlier partial inspection](linux-dependency-inventory-2026-11-04.md).

The inspection recorded 56 ELF roots, including the applicable QML imports,
16 conditional optional QML plugins, both available XCB GL integrations,
Qt libraries and tools, shipped ICU, WebEngine and the pinned host mpv root.
The Windows, macOS and iOS Controls records were classified as inapplicable
to Linux using pinned Qt declarative 6.9.3 module and feature metadata;
`Konvergo` was checked against the application's own registration. Qt/ICU
confinement was checked for Qt roots. Host mpv had a separate contract: its
root ELF hash was pinned, and its resolved host dependencies were checked for
canonical paths, ownership and mode. This did not establish package or hash
provenance for every host dependency.

The required missing SONAME union was `libxcb-cursor.so.0`; no missing SONAMEs
were reported for the conditional QML plugin roots. OpenGL, GLES2 and
xkbcommon pkg-config, header-syntax and compiler linker-file-resolution checks
passed. These checks did not perform a complete compile-and-link probe. Qt's
build features reported OpenGL enabled and GLES2 disabled; actual GLX/EGL and
optional QML plugin selection at runtime was not observed.

The combined apt simulation was explicitly a **conditional superset**, not an
installation instruction. It reported 0 upgrades, 1 new package and 0 removals:
`libxcb-cursor0` version `0.1.4-1build1`, architecture `amd64`. Current package
metadata recorded archive size 10,718 bytes and SHA-256
`137cf52479b5a9d8c5926d70d311af04be41941a32ee777340d704cc458c06d8`.
The package's reported installed size was 39,936 bytes; this is a metadata sum,
not net disk change or peak-space proof. Observed free space was 29,244,334,080
bytes on the candidate filesystem at inspection time.

No package was installed or downloaded, no index was updated, and the SDK was
not retried. The candidate remains consumed with READY absent. A persistent host
change requires separate authorization and review of the exact transaction.
After an approved correction, a fresh candidate must pass the full runtime,
CMake/private-target and compile/link probe before READY; this inspection is not
Linux application, desktop, playback or CI acceptance.
