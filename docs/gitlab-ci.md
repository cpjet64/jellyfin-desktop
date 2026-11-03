# GitLab native CI

The public primary is [GitLab project 314](https://gitlab.curtpme.com/cpjet64/jellyfin-desktop).
Push to the `gitlab` remote. GitLab's SSH push mirror copies branches and tags
to the public GitHub fork and retains divergent refs. All GitHub Actions are
currently disabled, including the separate release synchronization workflow;
the Git push mirror does not by itself copy release notes or assets. See
[release mirroring](release-mirroring.md) for that workflow's design and status.

The required GitLab pipeline has six native jobs: Windows x64, Linux x64, and
macOS Intel, each with a bundled static mpvqt configuration and a separately
installed system mpvqt compatibility configuration. The static jobs require
the pinned mpvqt submodule and set `USE_STATIC_MPVQT=ON` and
`MPVQT_EXTERNAL_EVENTS=ON`. System jobs use `USE_STATIC_MPVQT=OFF` and
`MPVQT_EXTERNAL_EVENTS=OFF`. Each job verifies its own prerequisites and builds
the candidate; one runner's artifacts do not establish another runner's prerequisites.

Branch pushes, merge requests, and web/API branch pipelines are admitted.
Push pipelines are suppressed when an MR pipeline covers the same branch.
Protected refs, tags, schedules and other pipeline sources remain excluded
pending the protected-resource policy. Jobs use unprotected runner boundaries
and contain no signing, release, deployment, or registry publication step.

Every job must pass native preflight and build checks, the required non-GUI
CTest cases, input and bundle checks, and seven separate GUI processes: the
WebEngine fixture and both renderer-loss and web-client containment fixtures
for playing, paused, and held-hook scenarios. Reports must contain the required
cases and successful, unskipped results. Playing and paused scenarios also
require their before/after images. Static jobs additionally require the mpvqt
event-ownership test; Linux requires the real MPRIS test on a private session
bus. Failure or absence of a prerequisite or required result fails the job.

Windows runs GUI fixtures through a task-owned hidden desktop and job object;
Linux uses job-owned Xvfb and private D-Bus sessions and records the rendering
backend. macOS requires a dedicated, authorized GUI session, an available GUI
domain owned by the runner's console user, and the dedicated-session assertion.
It refuses an existing user mpvqt configuration that would otherwise be read.
The runner must already have the required GUI/session facilities; the jobs do
not install host packages or change runner settings. Resource groups serialize
each platform's project jobs; they do not establish host-wide isolation.

Windows requires Python with venv, Windows tar and curl, VS 2022 x64 C++ tools
and redistributable, and at least 20 GiB free. It bootstraps missing 7-Zip,
CMake, Ninja, Node, and Inno Setup into the checkout from pinned downloads;
the Inno installer signature and extracted compiler are verified. Windows
also downloads Qt 6.9.3, its WebEngine, WebChannel and Positioning modules,
and primary and fallback mpv archives. The archives are checked against
SourceForge's published SHA-256 values before the larger Qt download.

Linux and macOS require their native compiler, CMake, Ninja, pkg-config, mpv,
Qt modules, Node and Python, plus at least 10 GiB free. Linux additionally
requires Xvfb, xauth, D-Bus and glxinfo. Each system job needs a compatible
installed mpvqt development package; missing packages fail rather than switch
to the bundled library. No host package installation is attempted. Each job
records its build and test evidence, and the Windows packaging job publishes an
unsigned installer, portable ZIP, checksums and provenance when successful.

The configuration describes required proof, not a claim that the six jobs
have passed. Verify runner eligibility, exact delivered SHA, terminal results,
and artifacts in the actual GitLab pipeline. A green pipeline does not alone
establish clean-machine installation, hardware-specific behavior, signing,
release readiness, or hands-on user acceptance.
