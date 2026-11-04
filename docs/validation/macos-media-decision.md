# Intel macOS media build decision

**Decision requested:** whether the separate yt-dlp helper may use the pinned
official macOS binary inputs described in [the helper alternative](macos-ytdlp-alternative.md).
The requested source-only media dependency proposal is not yet ready for
approval. The helper alternative is a scoped binary exception, not a completed
media build plan or evidence of parity. No acquisition or installation is
authorized by this document.

The existing macOS CI invokes unpinned `brew install mpv`; it does not identify
an Intel Tahoe artifact. The pinned Homebrew core commit is a discovery and
feature reference, not a build lock. Desktop links libmpv and uses embedded
rendering, audio, subtitle and network behavior. It defaults to `ytdl=false`
but applies manual settings later, so an enabled helper is a possible custom
configuration. The full expected behavior and tests are in the
[feature matrix](macos-media-feature-matrix.md).

Preserving the reference source route for yt-dlp brings Deno and its Rust,
LLVM and V8 build inputs. Upstream Deno's Cargo.lock has 1,130 package records;
that count does not identify the selected Tahoe build closure or prove offline
availability. The alternative pins official standalone yt-dlp and Intel Deno
release assets, totaling 80,263,335 compressed bytes according to release
metadata, but their expanded and temporary peak sizes, macOS compatibility,
signatures, licensing, helper discovery and behavioral equivalence remain to
be verified. A binary exception must be explicit and separately approved.

The media side still requires a verified immutable source/resource/patch
manifest, resolved platform conditionals and options, a fixed build order,
compatible Xcode and Python decisions, licensing review and storage peak.
Ten identified Git-derived archive digests have a bounded acquisition proposal,
but that list is not the entire closure. The currently observed 43.4 GB free
does not prove capacity. The [candidate graph](macos-media-candidate-graph.md),
[source](macos-media-source-index.md), [resource](macos-media-resource-index.md)
and [option](macos-media-option-index.md) indexes are inspection aids; they
must not be executed as a build recipe. The official Qt 6.9.3 input is separate.

Any future approved build must use a job-owned prefix, an explicitly selected
x86_64 SDK/deployment target and bounded concurrency. It must fail on missing
required optional features and implicit downloads, verify recursive Mach-O
dependencies and metadata, and exercise actual Desktop rendering, playback and
relevant custom helper behavior. Native GUI acceptance remains independent of
build-only validation.
