# Offline fetch-site ledger (source review only)

Provider: `1f8d2e76255abc48f0456d9be99b4cab9c6566e6`. The provider catalog
records exact SHA-256 identities and source lines; it is a static inventory,
not the evaluated dependency closure. The edits below have not been executed.

| Site | Evidence and proposed treatment | Status |
| --- | --- | --- |
| Root `CMakeLists.txt` lines 20, 23, 116 | Compiler, Meson and CPU feature `execute_process` calls. These execute host tools; they are not themselves demonstrated network fetches. | Unchanged; fixed tool identities and execution admission needed. |
| `cmake/download_externalproject.cmake` lines 3-21 | mkdir, unzip, curl, touch and patch during configure. Exact whole-file overlay includes a staged module. | Proposed removal; entire module and its include graph remain unverified. |
| `cmake/custom_steps.cmake` lines 117, 140 and later | Git fetch force-update and configure-time command inside `force_rebuild_git`, plus other reset and cleanup operations. | `staged_source.custom_steps` proposes removing that exact function body. SVN/Hg update and cleanup helpers remain unchanged. |
| `toolchain/rustup.cmake` | curl installer and floating rustup update. | Exact whole-file overlay removes both; complete staged Rust verification is outstanding. |
| `packages/*.cmake`, `toolchain/*.cmake` | ExternalProject Git/URL downloads, floating branches, submodule and archive resolution. `UPDATE_COMMAND ""` does not prevent initial clone. | Unchanged; each selected dependency needs an authenticated prestaged source and download/update suppression. |
| `packages/mpv.cmake` lines 28-31 | Initial Git download and update declaration. | `staged_source.mpv_recipe` proposes replacing this exact declaration with a prestaged source and empty download/update commands; not wired into `adapter.apply`. |
| 21 named Meson recipes in `recipe_edits.py` | Each original source hash and exactly one `meson setup` site is checked; preserve options and add `--wrap-mode=nodownload`. | Source proposal; dependency wraps still require staged inputs. |
| `packages/subrandr.cmake` | Exact source hash and one Cargo command; add `--locked --offline` and `CARGO_NET_OFFLINE=true` for descendants. | Source proposal; nested Cargo lock enforcement and vendor closure still require independent proof. |
| `packages/mpv-release.cmake` | Configure-time script reaches latest release via curl. | Unchanged; standalone release path is outside the intended library target but inclusion/reachability must be proven or denied. |
| `toolchain/cppwinrt.cmake` | `wget` of floating Windows.winmd. | Unchanged; selected GCC graph reachability remains unproven. |
| `scripts/autobuild.sh` and workflows | `ninja update`, packaging downloads and remote operations. | Must not be invoked; no claim of removal. |
| Nested upstream sources, Cargo build scripts and LFS | Trees, lockfiles, submodules, downloads and LFS payloads may have their own fetches. | Closure and actual consumption unknown; pointers and payload hashes must be authenticated before use. |

No complete fetch denial exists yet. Recipe edits do not make a build safe or
offline; in particular ExternalProject still generates source fetches. A
future execution boundary must reject unknown fetch behavior and enforce
network isolation independently. Preserve all provider libmpv feature roots.

The mpv binding contract requires the locked `mpv` Git row at commit
`01e2533f3143b8940857ca419b0b367f583e05ca`, a nonempty patch list, and
separately pinned checkout and post-patch verification callbacks. These
callbacks are not implemented by the producer yet. Its current checkout
verifier checks unpatched Git trees, not the required patched postimage. Its
archive and nested-submodule support also remains incomplete. Source and
patch verification, staged path binding and post-overlay tree verification
must be integrated before this proposal can be used.
