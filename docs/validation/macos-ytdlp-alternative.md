# Candidate isolated yt-dlp runtime alternative

For Pro review only: use official upstream Intel-compatible release artifacts
for the **separate yt-dlp helper**, while continuing to source-build libmpv and
its media libraries. This is a binary-input exception to the proposed all-source
closure and needs separate user approval. It does not establish feature parity.
Desktop defaults to `ytdl=false`, but manual settings can enable it; preserving
that custom capability requires the helper and JavaScript runtime to be
discoverable by mpv. Neither may be silently removed from acceptance.

| Exact proposed input | Size | Published SHA-256 |
| --- | ---: | --- |
| [yt-dlp 2026.10.26 universal macOS executable](https://github.com/yt-dlp/yt-dlp/releases/download/2026.10.26/yt-dlp_macos) | 37,466,304 bytes | `0f922c15ccc0b83eefa1c8bc9104e434318c4bf16e408e1ee93b7745a6efe0aa` |
| [Deno 2.9.8 Intel macOS ZIP](https://github.com/denoland/deno/releases/download/v2.9.8/deno-x86_64-apple-darwin.zip) | 42,797,031 bytes | `4b2f5edd2639b7470f034238b321d7002d8dfb4726d8558f52c17afac370eb2c` |

Sizes and digests are GitHub release-asset metadata observed 2026-11-04, not
locally verified bytes. Total compressed acquisition is 80,263,335 bytes.
The [yt-dlp documentation](https://github.com/yt-dlp/yt-dlp#release-files)
describes its macOS executable as universal and standalone; its documentation
also recommends a JavaScript runtime and identifies Deno as the default. The
executable has bundled third-party licensing obligations. Release metadata is
not proof of slice, minimum OS, signatures, code identity, runtime dependencies,
or equivalent behavior to Homebrew's Python environment.

Proposed writable locations are a new job-owned `media/downloads/`,
`media/helpers/` and `media/evidence/` below the approved project job root;
no global installation or PATH change. The launched application's private
environment must resolve `yt-dlp` and `deno` there, without network auto-update
or implicit helper download. Verify archive hashes and upstream signatures,
inspect macOS slices, minimum OS, signatures and recursive library dependencies,
record exact identities and licenses, then exercise manual `ytdl=yes`, the
default `ytdl=false`, representative extraction, and failure without remote
component fallback. Test the actual Desktop-launched process, not only CLI.

The exact expanded Deno size, yt-dlp's runtime temporary extraction, combined
peak, and licenses and feature equivalence are unmeasured. Capture free space
and apply a reviewed cap before acquisition; stop on capacity errors. This
alternative removes the need to build Deno, Rust, LLVM and their Cargo/V8
closure **only for this helper**. It does not resolve the media source graph,
SDK/Xcode requirement, or the source-lock and storage gaps for other components.
No artifacts have been downloaded or installed.
