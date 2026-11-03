# Release mirroring

The public primary repository is
[GitLab](https://gitlab.curtpme.com/cpjet64/jellyfin-desktop).
[GitHub](https://github.com/cpjet64/jellyfin-desktop) receives branches, tags and
commits through GitLab's SSH push mirror. Push to GitLab; divergent GitHub refs
are retained and reported as mirror errors instead of being overwritten.
The repository's default push destination is the existing `gitlab` remote;
the `origin` GitHub remote and upstream reference are preserved.

GitLab project 314 is the release source. GitHub polls its public release API
every ten minutes, on manual dispatch, and after relevant pushes to `master`.
Scheduled runs can be delayed by GitHub; this is not immediate synchronization.
The workflow uses GitHub's short-lived `GITHUB_TOKEN` with contents write access.
It neither builds nor signs assets and does not create a GitLab release.

Only releases whose `released_at` is no longer in the future are considered.
The GitHub tag must already exist and resolve to the exact GitLab release commit;
the separate Git push mirror supplies tags. A new GitHub release starts as a
draft and is published only after its assets upload. A marker in the notes
identifies releases managed by this workflow. A pre-existing unmarked release
is never changed. Existing assets must match both size and GitHub's SHA-256
digest; collisions or missing digests stop the run rather than overwrite data.
Assets removed from GitLab are not deleted from GitHub. Rename or removal
requires separate operator review.

Asset links and their bounded redirects must use HTTPS on `gitlab.curtpme.com`;
no GitLab credentials are sent. GitHub API requests never follow redirects.
The release notes contain an encoded asset manifest and a fingerprint of source
link names and URLs. When those match and GitHub's asset sizes and digests still
match, later runs skip downloading the bytes. This assumes an asset URL's bytes
are immutable; publish changed bytes at a new URL or rename the asset. Changing
bytes in place is not detected by the polling optimization.
Each response is limited to 512 MiB. Off-host links or redirects, larger assets,
missing tags, mismatches, API errors and partial uploads fail the run visibly.
After resolving the cause, dispatch the workflow again; matching uploaded assets
are reused. Check the GitHub Actions run and compare the release and asset
hashes before considering a release mirrored. A failed run is not proof of a
complete mirror.
Only the most recently released GitLab release is marked GitHub's latest;
earlier releases explicitly use `make_latest=false`. Future releases are skipped.

API references: [GitLab releases](https://docs.gitlab.com/api/releases/),
[GitLab release links](https://docs.gitlab.com/api/releases/links/), and
[GitHub releases and assets](https://docs.github.com/en/rest/releases).
