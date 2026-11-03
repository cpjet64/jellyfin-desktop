# Release mirroring

The public primary repository is
[GitLab](https://gitlab.curtpme.com/cpjet64/jellyfin-desktop).
[GitHub](https://github.com/cpjet64/jellyfin-desktop) receives branches, tags and
commits through GitLab's SSH push mirror. Push to GitLab; divergent GitHub refs
are retained and reported as mirror errors instead of being overwritten.
The repository's default push destination is the existing `gitlab` remote;
the `origin` GitHub remote and upstream reference are preserved.

GitLab project 314 is the release source. All GitHub Actions are currently
disabled, including the release synchronization workflow. Consequently, GitLab
release notes and assets are not automatically copied to GitHub by that
workflow. The SSH Git mirror described above continues to handle Git refs.

The remaining sections describe the existing workflow's behavior if it is
enabled in a separately authorized operation. Its configured triggers are a
ten-minute schedule, manual dispatch, and relevant pushes to `master`; scheduled
runs may be delayed. It uses GitHub's short-lived `GITHUB_TOKEN` with contents
write access. It neither builds nor signs assets and does not create a GitLab
release. The workflow's presence in the repository does not mean it is running.

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
The release notes contain an encoded digest manifest and separate source URL and
strong ETag validators. Each poll checks the source: it skips downloading bytes
only on a valid conditional 304 response when the existing GitHub asset also
matches its recorded size and digest. Missing or weak validators require a full
download and hash comparison. Changed bytes at the same URL cause a collision
failure rather than overwriting the existing asset. Removed source assets and
unexpected GitHub assets also fail for operator review; completed assets are never deleted.
The sole automatic deletion is an expected asset on a mirror-owned release
that GitHub reports as an empty `starter` with no digest and a valid asset ID,
the residue of a failed upload. The mirror first makes the release a draft,
deletes only that residue, then uploads and verifies the replacement. Other
incomplete or mismatched assets require operator review.
When adding assets to a published release, the mirror returns it to draft before
uploading. If upload fails, the release remains unavailable as a public release
until a successful retry; this avoids exposing a partially updated release.
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
