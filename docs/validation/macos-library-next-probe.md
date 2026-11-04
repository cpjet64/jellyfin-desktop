# Proposed read-only Xcode and media prerequisite probe

This is a plan only. No host commands, setup, downloads, compiler runs, builds, or Metal workloads were performed for it. The target is an existing Intel Tahoe host and a future isolated media build. The previous host probe's SDK 26.5 and Apple clang 21 observations do not establish a usable full Xcode, Metal toolchain, accepted license or completed setup.

## Requirements and sources

The pinned [MoltenVK 1.4.2 README](https://github.com/KhronosGroup/MoltenVK/blob/db66022459ffb663aa2b50f6b018bc2e124f5edf/README.md#building-moltenvk) states Xcode 15.0.1 or later is supported, based on its CI coverage. The pinned Homebrew formula's Xcode 11.7 minimum is lower and must not be used as the effective MoltenVK requirement. The reference formula invokes `xcodebuild` for its dependency and packaging projects, so Command Line Tools alone do not establish this prerequisite. [Apple's Xcode requirements](https://developer.apple.com/xcode/system-requirements/) pair Xcode 26.5 with macOS 26.5 SDK and specify supported host versions; that table does not establish that a particular installed Xcode can build this pinned source. The [Apple command-line reference](https://developer.apple.com/documentation/xcode/xcode-command-line-tool-reference) identifies `xcodebuild` as part of full Xcode.

Apple notes that Metal Toolchain may be an additional Xcode component and documents commands that install components in its [component instructions](https://developer.apple.com/documentation/xcode/downloading-and-installing-additional-xcode-components). This probe must not invoke those commands. Apple also warns that invoking developer tools on a fresh installation can prompt for Command Line Tools installation ([Apple instructions](https://developer.apple.com/documentation/xcode/installing-the-command-line-tools/)). The approved probe may query the exact existing full-Xcode executable's version and first-launch status; it must stop on any prompt or request for setup and must not accept it. Apple's [command-line reference](https://developer.apple.com/documentation/xcode/xcode-command-line-tool-reference) directs readers to the installed `man xcodebuild`; the documented `-checkFirstLaunchStatus` reports a nonzero status when system content requires installation. Treat errors and ambiguous output as unknown rather than repair instructions.

## Bounded future inspection

Use a read-only, noninteractive remote session only after the host and access route are independently authorized and the whole-VM lease is held. Record UTC observation time, host identity, command exit status and bounded non-secret output. Do not switch developer directories or run installation, first-launch, license, component, update or setup commands. Missing or inaccessible evidence is unknown, not evidence of absence. For each existing, resolved full-Xcode app only, use its exact `Contents/Developer/usr/bin/xcodebuild` path with a process-local `DEVELOPER_DIR`, noninteractive stdin and an explicit outer deadline for `-version` and `-checkFirstLaunchStatus` only; do not use the `/usr/bin` shim or `xcrun`. Exit promptly on prompts, timeouts or errors without remediation. Process-local selection does not change the host's persistent developer directory.

Use a 30-second deadline per finite query and a 16 KiB output cap per stream.
Track the exact launched process group; on timeout terminate only its owned
processes and verify exit. Record timeout and truncated output as non-success,
not as setup status. Do not start another query until the previous one settles.

1. Query the currently selected developer directory with `/usr/bin/xcode-select -p`; do not use `--switch`, `--install` or `--reset`. Record failure without remediation. This query identifies a selection only, not a working Xcode.
2. Inspect only the selected directory and the conventional `/Applications/Xcode.app` with `/bin/ls -ld`, resolving the actual selected path before reading further. A selected `/Library/Developer/CommandLineTools` is not full Xcode. Do not scan unrelated applications or user directories. If a full Xcode app is selected or its conventional path exists, read its `Contents/Info.plist` with `/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString'` and `-c 'Print :CFBundleVersion'`, using the resolved exact app path. Record identity and compare numeric version against 15.0.1; do not presume the app at the conventional path is selected.
3. Read the selected full Xcode's `Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs` directory with `/bin/ls -l`. For an exact existing SDK directory, inspect its `SDKSettings.plist` using `/usr/libexec/PlistBuddy -c 'Print :Version'` and `-c 'Print :CanonicalName'`; record missing keys or files. Check SDK 26.5 against the app's version and Apple's matrix. A directory name or an earlier compiler probe alone is not proof of that pairing.
4. Inspect only the selected app's `Contents/Developer/Toolchains` and `Contents/Developer/usr/bin` with bounded `/bin/ls -l` for existing Metal-related entries, and record exact paths and file metadata. Additional components can be stored elsewhere; absence from these directories is **unknown**, not proof that Metal is unavailable. Presence of a file is not a Metal compiler or GPU-functionality test.
5. If an exact full-Xcode binary exists, run only its `-version` and `-checkFirstLaunchStatus` as specified above. Record each exit code and bounded output; distinguish a reported pending task from an ambiguous failure. This check does not by itself prove license acceptance, Metal compilation, usable SDK linking or successful source build. If no full Xcode is found in the selected and conventional locations, report those bounded observations, not host-wide absence. A later user-approved installation, selection or setup would be required if no suitable existing Xcode is available.

Example read-only commands, after selecting an exact existing app path and SDK path from the observations above (replace placeholders locally with literal verified paths; do not execute unresolved placeholders):

```sh
/usr/bin/xcode-select -p
/bin/ls -ld /Applications/Xcode.app
/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' '/Applications/Xcode.app/Contents/Info.plist'
/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' '/Applications/Xcode.app/Contents/Info.plist'
/bin/ls -l '/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs'
/usr/libexec/PlistBuddy -c 'Print :Version' '/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX26.5.sdk/SDKSettings.plist'
/usr/libexec/PlistBuddy -c 'Print :CanonicalName' '/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX26.5.sdk/SDKSettings.plist'
/bin/ls -l '/Applications/Xcode.app/Contents/Developer/Toolchains'
/bin/ls -l '/Applications/Xcode.app/Contents/Developer/usr/bin'
/usr/bin/env DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer' '/Applications/Xcode.app/Contents/Developer/usr/bin/xcodebuild' -version
/usr/bin/env DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer' '/Applications/Xcode.app/Contents/Developer/usr/bin/xcodebuild' -checkFirstLaunchStatus
```

These examples inspect the conventional app only; adapt to the exact selected app without changing selection. Run the two SDK plist reads only if that exact SDK directory was observed; its name is an example rather than a claim of presence. Run the two Xcode queries only after resolving an existing exact app and executable, inside the deadline and output-limiting wrapper. Capture the literal selected path, app and build versions, SDK entries and plist values, visible Metal-related filenames and individual command exit statuses. Limit output if a directory is unexpectedly large, and never use recursive scans or write logs outside approved job-owned storage. Do not use `xcrun --find metal`, run first-launch setup, or accept a license.

## Future isolated mpv recipe corrections

The pinned [mpv 0.41.0 Meson options](https://github.com/mpv-player/mpv/blob/v0.41.0/meson.options) define separate feature options. Explicitly set `-Dhtml-build=disabled -Dmanpage-build=disabled -Dpdf-build=disabled`; do not rely on defaults or `auto`. Exclude docutils only after confirming no other retained component or build step consumes it. The documentation exclusion is approved as a build-only difference; it is not proof of runtime equivalence or authorization to omit other features.

The pinned [mpv Meson build](https://github.com/mpv-player/mpv/blob/v0.41.0/meson.build) adds `-L/usr/local/lib` in `objc_link_flags` and applies those flags to both C and Objective-C links. An isolated future recipe must remove that explicit host search path with an exact, checked source transformation or a validated equivalent, while retaining the needed SDK/system settings and adding only approved prefix paths. Assert the expected source match and inspect the resolved link commands and artifacts; do not treat the transform as completed or infer that it eliminates all host fallback paths.

The resulting next-step report should distinguish observed selected app, installed versions, SDK metadata, first-launch status and visible Metal files from unverified license, actual Metal compilation, build and playback readiness. Do not label the host ready solely from filesystem presence.

## Six-source static qualification preparation

The separate `dev/ci/macos_sdist_manifest.json` identifies the five proposed
Python backend sdists and VapourSynth 82: six files totaling 5,421,756 bytes,
with exact URLs, lengths and SHA-256 values. `macos_sdist_static.py` and its
Windows supervisor are **drafts for review, not cleared for acquisition**.
They enforce an 8 MiB transfer limit, 256 MiB aggregate decompressed-stream
limit and a 512 MiB workspace ceiling, and inspect metadata without extracting
or executing source files. The supervisor hash-binds the interpreter, worker
and manifest; creates a new proof directory; checks an additional 2 GiB
free-space margin; and launches one isolated `-I -S -B` worker hidden, with a
180-second deadline and direct-child kill/reap. The worker allocates and
verifies a 480 MiB reservation under that deadline before network access.
This relies on the reviewed worker's no-child contract. Independent
review, concurrent storage commitment assessment, and a host-qualified Python
identity remain required. Storage-full or quota failure stops without a later
receipt write or reservation deletion; all other failed or timed-out worker
results likewise retain the partial artifacts and reservation without writing
a success receipt. The worker disables proxy discovery and refuses redirects.

The conservative archive parser accepts bounded local PAX path, size and mtime
records, but rejects other PAX/GNU extensions, links and non-regular entries;
it also rejects duplicate or POSIX-normalized member names without case-folding
or Unicode normalization. Actual source-archive compatibility is unproven. On an
unsupported archive or other failure, retain all evidence and do not retry
unchanged. The inspector lists all member names but captures only selected
build metadata and backend/subproject text, so uncaptured references must be
identified and reviewed separately before claiming source closure. The draft
does not install, import or invoke the archived code. Flit 3.12 remains
separate from the reference flit 4.1 bootstrap and no Python substitution is
assumed. The 89-node build, Git archive acquisitions and total build-space
claim remain held.
