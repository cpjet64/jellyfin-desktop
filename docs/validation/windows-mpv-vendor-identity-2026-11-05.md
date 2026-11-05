# Windows libmpv vendor identity — 2026-11-05

One bounded comparison used the primary `x86_64-v3` archive selected by `dev/windows/ci.ps1` at source `993e6295887990a36e680ee74901d9ca66521588`. The [vendor archive](https://downloads.sourceforge.net/project/mpv-player-windows/libmpv/mpv-dev-x86_64-v3-20261018-git-01e2533f31.7z) was 32,855,420 bytes and SHA-256 `49533e80d681672e42ab38fccfdb9db0cc9fc423f86d34f5f9634eba71109912`, matching the CI pin. The download used HTTPS redirects only, a 64 MiB transfer cap and a 120-second timeout. An initial restricted-network connection failed without producing a file; the same authorized download succeeded with normal network access.

After archive hash and 7-Zip signature verification, a technical listing showed eight entries and exactly one `libmpv-2.dll`, an unencrypted regular member declared as 123,893,760 bytes. The listing showed no duplicate, link, special or unsafe path. Only that exact member was extracted to a fresh task-owned directory. The sole output was a regular non-reparse file of the declared size.

The frozen task budget was 512 MiB, with a 400 MiB maximum declared member size checked before extraction. The 7-Zip command itself did not enforce a hard output-byte cap; the check was against archive metadata before extracting the pinned archive.

| Compared file | SHA-256 |
| --- | --- |
| Extracted vendor `libmpv-2.dll` | `5c5c9d094859bdc60b8b27b2d832db8fa8c2e487efbd35f5aea5afe6960dad39` |
| Selected root SDK `dev/windows/deps/mpv/libmpv-2.dll` | `5c5c9d094859bdc60b8b27b2d832db8fa8c2e487efbd35f5aea5afe6960dad39` |
| W554 Release staged `src/libmpv-2.dll` | `5c5c9d094859bdc60b8b27b2d832db8fa8c2e487efbd35f5aea5afe6960dad39` |
| W554 Release staged `tests/libmpv-2.dll` | `5c5c9d094859bdc60b8b27b2d832db8fa8c2e487efbd35f5aea5afe6960dad39` |

The listing and extraction used installed 7-Zip 26.02 x64, executable SHA-256 `83967f1b02b43c4efeda302795722c809e0e81b8307de73558d10484d5676a7d`. The download used Windows curl 8.21.0, executable SHA-256 `73d24149ff289afc49ec41f08918ef9faa727d39ad993e929757dc2ddafab805`. The archive and extracted comparison member are retained under ignored `deps/w660-vendor-compare` in the integration worktree.

The archive name claims revision `20261018-git-01e2533f31`; the previously observed embedded version was `v0.41.0-1205-g01e2533f3`. These are version metadata, not a reproducible build or proof of the full commit and build inputs. This comparison establishes byte identity with the archive selected by the existing CI pin, subject to trust in that pin and vendor archive. It is not independent vendor signing verification, a DLL load, native execution, playback, recovery or product acceptance result. The SDK and staged files were read only.
