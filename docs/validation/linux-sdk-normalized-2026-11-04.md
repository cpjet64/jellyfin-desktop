# Linux SDK candidate: normalization and runtime blocker

On 4 November 2026, a standalone native Linux prerequisite experiment used the
retained candidate from source `3a2e46a20d4707a61544add409300d2b5dadd654` and
the normalization and probe code from `9df10a819c39a35555bb8e32e26b42420f4c84dd`.
This was not an application build or an exact-commit CI pipeline.

Before the one completion attempt, the candidate preflight checked the original
source identity, ten pinned archive sizes and hashes, 70 original pkg-config
files and the exact 156-link inventory. A privileged process inspection found no
visible workspace references and no uninspectable processes. These checks did
not attest every byte in the candidate tree. The runner was observed idle; that
observation did not reserve the runner.

The completion attempt normalized the 70 Qt pkg-config files and recorded its
normalization manifest. Its host-zlib baseline comparison passed using the
default pkg-config behavior, and the real SDK probe passed its Qt metadata and
private-header checks before reaching runtime dependency inspection. This is
evidence of those checks, not proof that every later build or playback dependency
is available.

The first runtime failure was the Qt XCB platform plugin: its dependency
`libxcb-cursor.so.0` was not found. The probe stopped there; it did not configure
or build the CMake test executable. The SDK READY receipt was absent. The
completion and normalization markers remain present, and this partially changed
candidate is consumed for the one authorized completion attempt. It must not be
treated as a ready SDK or retried as an untouched candidate.

The local retained completion receipt, bounded failure log and original failed
candidate evidence remain in the task's private proof directory. No application
build, successful SDK probe, Linux desktop acceptance, or CI success is claimed.
