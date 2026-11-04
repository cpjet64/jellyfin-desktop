# Linux dependency inventory: partial result

On 4 November 2026, one read-only aggregate inspection ran against the consumed
Linux Qt candidate described in [the SDK failure report](linux-sdk-normalized-2026-11-04.md).
The inspector was `586eb3df740d8d1345c44f564760c7d61bacbcbaf86df70be6a9803089c08919`
by SHA-256; this is a harness identity, not a Git commit. The retained receipt
has SHA-256 `55eff151e45c13b416b402c78406fc50c4b877c9a3474b72ade5110238b62a29`.
The process exited 1 and its output was not truncated. No package installation,
download, index update, SDK retry or application build occurred.

The inspection recorded 29 ELF roots, including the Qt modules, shipped ICU,
both available XCB GL integrations, WebEngine process, Qt build tools and the
selected host libmpv. The observed unresolved SONAME was `libxcb-cursor.so.0`
in the XCB platform plugin and both conditional GL integrations. Qt build
features reported OpenGL enabled and GLES2 disabled; actual XCB integration
selection was not observed. The OpenGL, GLES2 and xkbcommon pkg-config,
header-syntax and compiler linker-file-resolution checks passed. Those checks
did not perform a full compile-and-link probe.

The aggregate was incomplete for two inspector limitations:

- The QML scanner returned 33 records, including platform-specific Windows,
  macOS and iOS Controls imports without Linux paths. The generic unresolved
  import rule stopped at `QtQuick.Controls.Windows`, before the scanner-derived
  native QML plugins were inspected. `Konvergo` is the application's own module.
- The pinned host libmpv resolved its host ICU 74 libraries without a missing
  SONAME. The inspector incorrectly applied the Qt candidate's ICU 73
  confinement rule to that separate host libmpv root. This is not evidence that
  libmpv is broken or that these two ICU versions conflict in the application.

The inspection therefore did not establish the full required dependency union,
and it did not run the combined apt simulation. No package transaction, size or
persistent host change is proposed from this partial result. The runner had
29,244,375,040 bytes free at the observed candidate filesystem; that is not a
transaction peak-space calculation.

A bounded next inspection could classify platform-conditional QML scanner
records against Linux selection before inspecting the applicable native plugins,
and apply separate dependency-path rules to Qt/ICU roots and the verified host
libmpv closure. It should retain all partial records, map any required missing
SONAMEs to current package metadata, and only then simulate a clearly labeled
combined transaction. The consumed candidate and its markers must remain intact;
this proposal does not authorize a package installation or another SDK attempt.
