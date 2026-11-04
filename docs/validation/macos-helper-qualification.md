# macOS helper qualification: incomplete

The manifest in `dev/ci/mac_helpers_manifest.json` records the two approved
release identities and ceilings. The official Deno executable checksum file
was retrieved separately: 71 bytes, SHA-256
`4af009c8722536813a381182ce8afcd2504ccb8ef59dca34854bae72bec4562d`;
its content identifies the executable digest in the manifest. This is metadata,
not verification of downloaded executable bytes.

`mac_helpers.py` contains offline building blocks. The acquisition draft in
`mac_helpers_acquire.py` verifies exact sizes and hashes, stages in an empty
private parent, applies budgets and serializes acquisitions through a shared
repository `deps` lease. The
macOS entry point checks ownership and canonical ancestors, reserves the
operator-supplied concurrent storage commitment and restricts redirect hosts.
Consumers must use `verified_paths`, which requires `READY` and rechecks the
helper hashes. The marker is closed before an atomic no-replace hard link makes
it visible, after verification completes. The task-owned workspace must remain
private and exclusive; other readers must not inspect incomplete staged files.
`READY` means only that acquisition and Deno extraction hashes passed. It does
not establish provenance, signatures, Mach-O properties, licenses, extraction
bound or permission to execute.
This draft still requires independent review and publication before use. No
large release asset was acquired and no helper was run. The CLI argument builder is for a direct
invocation only, not mpv's unordered option map.

Before acquisition, independently review serialized admission, at least 4 GiB
free space after concurrent commitments, the 2 GiB complete workspace ceiling,
the 96 MiB acquisition and 8 MiB metadata ceilings, and failure-safe publication.
Only after the reviewed harness is committed and
pushed may the two exact assets be acquired once. Preserve originals and retain
checksum, available signature and identified-key provenance.

Before execution, inspect actual x86_64 Mach-O slices, minimum OS versions,
signatures as shipped, recursively resolved non-system dependencies, embedded
Python/resources/native libraries and EJS, and exact bundled notices. Establish
the 512 MiB self-extraction bound statically using reviewed upstream PyInstaller
inspection tooling or a separately pinned, reviewed inspection dependency.
TMPDIR and disk polling do not enforce that bound. Until it is proven, yt-dlp
execution remains blocked. Do not thin, re-sign, or change quarantine to force
execution.

The pinned yt-dlp source's [macOS requirements](https://raw.githubusercontent.com/yt-dlp/yt-dlp/f264a6cbd1f630894da0f368df00f8aae50140e0/bundle/requirements/macos.txt)
pin PyInstaller 6.22.0 and hooks-contrib 2026.8; this does not prove the producer
of the downloaded artifact. Review of PyInstaller's bootloader found that a sum
of declared TOC lengths alone is insufficient to prove an extraction bound.

Native standalone tests, a frozen local EJS fixture proving the actual selected
Deno provider and arguments, and real Desktop acceptance remain open. Use fresh
task-owned caches including DENO_DIR and disable Deno's update check. Quiet logs
alone do not prove the absence of downloads. The mpv option map must not contain
both `no-js-runtimes` and `js-runtimes`; verify its selected provider separately.
