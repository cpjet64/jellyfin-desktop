# Linux portable proof protocol for 8358bd6

This protocol is a review candidate, not a Linux run or a CI result. Its frozen source is `8358bd61286fb2d706f245a278aa577ef27363e1`; it does not qualify later revisions or newer gates. Execution requires a separate authorization and fresh runner, interpreter, workspace and placement checks.

The [manifest](../../dev/ci/portable_proof/manifest-8358bd6.json) identifies 69 source files and 26 named portable suites. The bundle is 156,208 bytes, SHA-256 `ee9daf887e9a2841f22b3dda9dedeaa203d278eabdf570aea3177fdbd178f07c`. It includes the tracked Linux Qt inventory required by one suite. A local unpacked Windows check passed 238 tests and skipped two Linux-only symlink cases; it does not prove Linux execution.

| Offset | Bytes | Chunk SHA-256 |
| ---: | ---: | --- |
| 0 | 65,536 | `67f30c7723a2a8f9564f2db44e049082b9988fe4deb8e0f21a6bade196b64303` |
| 65,536 | 65,536 | `48e4e9540da4475c6a6a549cce92d1b23a55eeb357164faacc38b455b89bf2fc` |
| 131,072 | 25,136 | `ce49a1527a009df3ac3e4b1dcdd648ea893acab637cb1de54dcd7f919703e076` |

The transfer uses a fresh runner-owned workspace and an exclusive journaled operation for each step. The controller is one live session only: interruption closes it, and existing journals are never resumed. File flushes do not prove Windows directory entries survive a crash; a missing journal is not permission to retry. A separate fresh-run authorization, independent of journal presence, is required. Source and helper identities, chunk order, sizes and hashes, complete bundle hash and manifest members are checked before suite execution. A submitted operation with unknown outcome is not resubmitted. Status responses must use the raw-first terminal collector, retaining partial or uncertain responses without repolling. All full serialized requests are bounded to 100,000 bytes.

With the pinned bundle and a 32-character task ID, the URL-encoded request bodies measured locally are 4,272 bytes for init, 97,138, 97,210 and 39,715 bytes for the three append operations, 4,274 bytes for finish, and 18,039 bytes for execution. A separate manual local check passed all three pinned-bundle append bodies through an injected fake submission client and confirmed byte identity. The committed automated test uses a synthetic chunk to check the same client boundary. These are preparation measurements, not wire evidence; this draft does not select or connect a guest transport. A real transport must preserve those bytes, the journal and terminal response.

The executor is designed to run the named suites separately under owned process groups with deadlines and settle each group before recording its result. Local supervisor tests use mocks; native process lifecycle remains unproven. On Linux, the Windows PowerShell test is the only permitted skip; native Linux symlink and publication cases must run. The process-group boundary does not contain a deliberately escaped session. No SDK installation, GUI, playback, native application build or CI claim is part of this protocol.
