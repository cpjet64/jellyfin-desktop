# GitLab native CI

The public primary is [GitLab project 314](https://gitlab.curtpme.com/cpjet64/jellyfin-desktop).
Push to the `gitlab` remote. GitLab's SSH push mirror copies branches and tags
to the public GitHub fork and retains divergent refs. All GitHub Actions are
currently disabled, including the separate release synchronization workflow;
the Git push mirror does not by itself copy release notes or assets. See
[release mirroring](release-mirroring.md) for that workflow's design and status.

The required GitLab pipeline has six native jobs and a separate product acceptance
job. The native jobs cover Windows x64, Linux x64, and
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

The native jobs record build/core, offscreen rendering and desktop acceptance
separately as passed, failed, blocked or not run. The offscreen renderer and
Linux MPRIS active Stop fixtures are held before launch while their render-thread
contract is unresolved. Historical offscreen-render-v1 and MPRIS offscreen
receipts are invalid for acceptance. A successful
headless job does not establish desktop acceptance. The acceptance job requires
all six matching evidence sets and separately checks WebEngine, windowed
playback, containment and applicable platform callbacks. Missing producers are
reported as blocked; a status string alone cannot establish a passing receipt.
No new receipt is emitted for these held fixtures.

Windows records its core and runtime CTest cases together in
`headless-ctest.xml`. Linux and macOS record the pure core selection there and
the additional guarded runtime cases separately in `runtime-ctest.xml`. The
runtime report does not prove desktop or native-controls acceptance.

On Linux and macOS, the job first builds and runs an explicit headless set:
system, logging, settings, display-manager mock, log rotation, Chromium flags,
WebChannel transport, the SMTC admission model, and the pure mpv test-bootstrap checks. These are
application-less or QCoreApplication tests; the selection does not infer that
every test without a `gui` label is nonvisual. Node and portable packaging
fixtures also run before desktop verification. The remaining CTest cases,
including QGuiApplication, Quick, mpv initialization, and MPRIS cases, are
deferred until the platform's runtime prerequisites are checked. Linux fails
when its required display facilities are unavailable. On macOS the pure
headless work is retained when an authorized existing session is unavailable,
and the offscreen runtime and desktop obligations are recorded as blocked.

Linux guarded runtime tests use job-owned Xvfb and private D-Bus sessions.
macOS requires the explicitly authorized `runner` account on
`tahoe-runner-114` (GitLab runner 12), a pre-existing GUI domain and matching
console ownership. The job checks `CI_ALLOWED_GUI_ACCOUNT` and
`CI_ALLOWED_GUI_HOST` against that identity before internally enabling the test
session guard. It never creates a session, logs in, or changes an account. It
refuses an existing user mpvqt configuration that would otherwise be read.
The runner must already have the required GUI/session facilities; the jobs do
not install host packages or change runner settings. Resource groups serialize
each platform's project jobs; they do not establish host-wide isolation.

The C2 native retirement fixture is also held before launch. Its pure state test
is not part of the hold, but a separate CI stage for it is not yet wired.
Windows loader, installer and packaging checks remain enabled.

Windows requires Python with venv, Windows tar and curl, VS 2022 x64 C++ tools
and redistributable, and at least 20 GiB free. It bootstraps missing 7-Zip,
CMake, Ninja, Node, and Inno Setup into the checkout from pinned downloads;
the Inno installer signature and extracted compiler are verified. Windows
also downloads Qt 6.9.3, its WebEngine, WebChannel and Positioning modules,
and primary and fallback mpv archives. The archives are checked against
SourceForge's published SHA-256 values before the larger Qt download.

Linux and macOS require a native compiler, pkg-config, mpv, Qt modules, Python,
and at least 10 GiB free. Linux additionally requires installed Xvfb, xauth,
D-Bus and glxinfo. Linux can bootstrap missing CMake, CTest, Ninja and Node into
a job-owned prefix from pinned official archives. Inventory reports the observed
distribution and installed/candidate display-package versions; it does not
install host packages. Qt and mpv must be available from standard locations or
the explicit `CI_QT_PREFIX` and `CI_MPV_PREFIX` paths. macOS requires its native
tools and a separately authorized dedicated GUI session; inventory does not
create or attest that session.

The optional Linux split-SDK route requires all of `CI_MPV_DEV_DEB`,
`CI_MPV_INCLUDE_DIR` (the extracted `usr/include`) and `CI_MPV_LIBRARY` (the
installed real DSO). The job verifies the pinned development archive and
extracted headers, checks the runtime package identity and version, then builds
and runs a no-core API/linkage probe before configuring. Invalid explicit inputs
fail without falling back to pkg-config. The explicit route also requires mpv
pkg-config metadata to be absent, preventing it from redirecting CMake's library
selection. Probe identity describes the probe, not the eventual application or
the system MpvQt package. Supplying these inputs does not install
or acquire either package, nor establish runtime or display acceptance. The
system job still requires its independently supplied MpvQt package.

System jobs use an independently installed mpvqt package. Windows builds
MpvQt at pinned revision `2ebbd38` as a shared library in a job-owned prefix,
applying the destructor compatibility patch only to the verified build copy; it
does not apply the external-events patch to this system-compatibility path.
See [MpvQt compatibility](mpvqt-compatibility.md) for the patch and evidence.
The build uses pinned KDE ECM 6.31.0 and the job's Qt and mpv inputs. System mode
requires CMake 3.30 or newer for ECM. A focused configure/build check exercises
ECM's missing-formatter branch before building mpvqt. The job verifies the
imported runtime lies in that prefix and records the application's shared-library
linkage. This is an installed-package compatibility check using the job's own
build, not proof of a separately supplied vendor package. Missing prerequisites
fail rather than switch to the bundled implementation. Windows system packaging
also requires the installed mpvqt runtime to be staged by the bundle rules.
Each job records build and test evidence; the Windows packaging job publishes
an unsigned installer, portable ZIP, checksums and provenance when successful.

Qt's Windows known-folder paths do not necessarily follow `APPDATA` and
`LOCALAPPDATA`. The CI test scope sets task-owned environment paths as defense
in depth; test-side pre-initialization checks must prove actual Qt configuration
and cache paths before mpv starts. A renderer readiness or scene-graph failure
triggers a separate hidden-desktop OpenGL diagnostic while retaining the
original failure. Other failures do not trigger the diagnostic.

The configuration describes required proof, not a claim that the six jobs
have passed. Verify runner eligibility, exact delivered SHA, terminal results,
and artifacts in the actual GitLab pipeline. A green pipeline does not alone
establish clean-machine installation, hardware-specific behavior, signing,
release readiness, or hands-on user acceptance.
