# Decision proposal: static bound for the macOS yt-dlp helper

**Decision requested:** permit a narrowly scoped, independently reviewed static
verifier for the exact approved `yt-dlp_macos` 2026.10.26 bytes, or retain the
execution blocker. This is not a general archive extractor. Acquisition and
non-executing asset inventory can continue independently under their separately
reviewed limits. No helper execution is authorized by this proposal.

## Source evidence and its limit

The [pinned yt-dlp workflow](https://github.com/yt-dlp/yt-dlp/blob/f264a6cbd1f630894da0f368df00f8aae50140e0/.github/workflows/build.yml)
installs its [hash-locked macOS requirements](https://github.com/yt-dlp/yt-dlp/blob/f264a6cbd1f630894da0f368df00f8aae50140e0/bundle/requirements/macos.txt)
and builds a universal2 one-file binary. Those requirements pin PyInstaller
6.22.0 and its hooks 2026.8. This establishes intended build inputs, not proof
that the downloaded bytes came from that workflow or contain that bootloader.
The verified release asset identity, signature condition and actual embedded
format must be recorded separately.

The official [6.22.0 reader](https://github.com/pyinstaller/pyinstaller/blob/v6.22.0/PyInstaller/archive/readers.py)
finds the archive cookie by scanning backward for its magic; the cookie gives
archive length and TOC offset and length. Each variable-length TOC entry records
offset, compressed length, declared uncompressed length, compression flag,
type and name. The [archive viewer](https://github.com/pyinstaller/pyinstaller/blob/v6.22.0/PyInstaller/utils/cliutils/archive_viewer.py)
can list these fields and embedded PYZ content. It is useful corroborating
inventory, but its reader stores named entries in a dictionary, so duplicate
names can be lost. Its extraction path materializes output in memory.

More significantly, the matching [bootloader decompressor](https://github.com/pyinstaller/pyinstaller/blob/v6.22.0/bootloader/src/pyi_archive.c)
writes compressed-stream output until the stream ends without enforcing the
TOC's declared uncompressed length. A sum of displayed lengths alone therefore
does not prove the hard bound. The [extraction loop](https://github.com/pyinstaller/pyinstaller/blob/v6.22.0/bootloader/src/pyi_launch.c)
also treats binaries, data, zip files, symlinks and multipackage dependencies
as extractable. Actual type and bootloader behavior must be established, not
inferred solely from the source pin.

## Proposed verifier contract

After identity verification, inspect the unchanged file with the reviewed
upstream viewer and an independently reviewed, exact-format verifier. The latter
would enumerate the raw TOC without collapsing entries and validate cookie,
record boundaries, offsets, lengths, compression flags, names and types. Reject
duplicates, unsafe or conflicting paths, malformed or overlapping ranges,
unsupported types and external/multipackage dependencies. Resolve any splash,
symlink or other special extraction behavior explicitly or fail closed. Cap TOC
bytes, entry count, processing time and retained evidence before processing.

For every potentially extracted compressed entry, stream decompression to a
discarding bounded sink, enforce per-entry and aggregate limits during inflation,
require the expected stream end and consumed input, and compare measured output
with the declared length. Validate uncompressed entry ranges and lengths too.
Record each entry and measured sum, including bundled libraries and resources;
inspect the bundled EJS identity separately. The sum of simultaneously extracted
file bytes must be at most **512 MiB**. Any ambiguity, unsupported format or
missing proof blocks launch. Use fixed-size buffers; do not materialize a
general extraction tree or execute archive contents. Test malformed, duplicate,
truncated, oversized and misleading-length fixtures before asset inspection.

The 512 MiB limit concerns extracted file bytes, not directory metadata, file
allocation rounding, caches, logs, other task-owned assets or staging. Account
for those separately within the **2 GiB total helper workspace** and require at
least **4 GiB free** after concurrent commitments before acquisition. Preserve
the original helper bytes and hash them before and after inspection. Static
inspection is not proof of a runnable helper, no implicit downloads, or Desktop
GUI acceptance; those remain separate gated checks.
