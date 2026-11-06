# Pipeline 32699: terminal failure evidence

Pipeline 32699 ran commit `52391b95f59bc8820845f1cc7509d80f6c132b88` on
`codex/windows-issues`. Its final status was **failed**; all seven jobs reached
terminal failure. This is evidence for that exact pipeline, not a native or
product-readiness qualification.

| Job | Configuration | Earliest meaningful failure |
| --- | --- | --- |
| 98248 | Windows x64 | Isolated selector help failed its bootstrap source-hash assertion. |
| 98249 | Windows system | The same isolated selector help test failed its source-hash assertion. |
| 98250 | Linux static | Qt cache test `test_ready_receipt_binds_all_copied_hashes` rejected `qt-host-zlib.json` as invalid normalization evidence. |
| 98251 | Linux system | The same Qt cache test rejected `qt-host-zlib.json` as invalid normalization evidence. |
| 98252 | macOS static | Fixture-copy platform selection's simulated Windows case failed CMake generation: `CMAKE_CXX_CREATE_STATIC_LIBRARY` was not set. |
| 98253 | macOS system | The same fixture-copy platform selection case failed CMake generation. |
| 98254 | Acceptance aggregate | Reported failed build evidence and blocked desktop and native prerequisites. |

The Linux validator checks regular-file type, size, link count, ownership and
write permissions; its trace does not identify which predicate failed. The
test creates the evidence file with default permissions, making the runner's
umask a possible explanation, not an observed cause. The macOS test fixture
declares no project language while requesting a CXX-linked static target;
the trace does not by itself establish why a specific CMake setup failed.
The Windows source has LF attributes and LF pins, and its runner reused an
existing checkout. Stale checkout line endings are possible, but the runner's
input hashes were not captured and the precise mismatch is unproven.

The aggregate's unavailable and held native, desktop, WebEngine and playback
cases remain blocked. No retry, cancellation, guest or host-setting change was
performed as part of monitoring. Full traces and six published artifact archives
are retained in the integration worktree's ignored `deps/w794-proof/` directory;
the aggregate published no archive. SHA-256 values:

| File | SHA-256 |
| --- | --- |
| `98248.log` | `76c64760bcb6ce05073ce0aa2c479097102eaa01b745f801fa0221e77c8ce4ed` |
| `98248.zip` | `9f919cfef61d5bed5d5f69241191e8ffb9c6ad57e4186535040eb5965b7a0673` |
| `98249.log` | `3dd63970396ed0f2771fcb5cb0d5bc8e6344098847fb14bec0a55fd6c7a39604` |
| `98249.zip` | `55c21f8181785fa7cb35269181d3975395ae50b4bd5ce8e09348bcbe65be0bc1` |
| `98250.log` | `252c98664dc7cef1b8208cec9d74ddaa557b265b52199d9f25cbb340d00b3a77` |
| `98250.zip` | `8d0d628ac855c9bdbb97334282a590aed2a8fc7909c1cb78c32f2b79f5229dab` |
| `98251.log` | `13d7bcba0cc5d30997546507c1d0c9e070e1b28bae63462da407d4f0f90b82a6` |
| `98251.zip` | `1a7c94af209a3e004849a12a25ef679f4b5c5e25351615d6312114fd0454988b` |
| `98252.log` | `b831dfdd092dcdcf8549ada434623ee7476b8e9e03313961ff602721fb9f2e3c` |
| `98252.zip` | `321c27264e1754854a0be98752d812ae526299c099fbff9823155c07bcb28a81` |
| `98253.log` | `e815324df2f01d9840f86f77827ec4dc8aa22ff0635159349892956bc4ccd8b3` |
| `98253.zip` | `b2c78198a1c3b95513bb377830b59816c407b530b091b5784b68243fb9da397f` |
| `98254.log` | `9e14ef8cf06512e232802018e14b388fdda3708e17fcdf7c7dd5b9156c8c63b8` |
