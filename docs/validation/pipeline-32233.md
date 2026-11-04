# Pipeline 32233 evidence — 2026-11-04

[GitLab pipeline 32233](https://gitlab.curtpme.com/cpjet64/jellyfin-desktop/-/pipelines/32233) ran commit `4a789dd652371b7eb03904d2ea9ee40896700b9b` on `codex/windows-issues`. All seven jobs reached terminal states. This pipeline does not test later commits, including `3ff7f6c` or `47e7f0b`.

| Job | Result | Evidence |
| --- | --- | --- |
| [Windows static 97367](https://gitlab.curtpme.com/cpjet64/jellyfin-desktop/-/jobs/97367) | Passed | Artifact upload succeeded. |
| [Windows system 97368](https://gitlab.curtpme.com/cpjet64/jellyfin-desktop/-/jobs/97368) | Passed | Render and desktop obligations remained held or blocked. |
| [Linux static 97369](https://gitlab.curtpme.com/cpjet64/jellyfin-desktop/-/jobs/97369) | Failed | Qt and mpv development inputs were not found. |
| [Linux system 97370](https://gitlab.curtpme.com/cpjet64/jellyfin-desktop/-/jobs/97370) | Failed | Qt, mpv and system MpvQt configuration were not found. |
| [macOS static 97371](https://gitlab.curtpme.com/cpjet64/jellyfin-desktop/-/jobs/97371) | Failed | The mpv version check failed because pkg-config could not find mpv. |
| [macOS system 97372](https://gitlab.curtpme.com/cpjet64/jellyfin-desktop/-/jobs/97372) | Failed | The same mpv version check failed. |
| [Product acceptance 97373](https://gitlab.curtpme.com/cpjet64/jellyfin-desktop/-/jobs/97373) | Failed | Unix build failures and blocked render and desktop evidence prevented acceptance. |

The Windows static job's uploaded archive passed ZIP integrity validation. Its own build manifest identifies the pipeline commit and static MpvQt mode. The installer and ZIP entries were independently hashed and matched the job's checksum file:

- Installer: `294706727b3e9a63b7e0911cfb6b6eecf9cdaae1e8ba2e71881ba84123ee25f3`.
- ZIP: `66bf584773db740b293d906ca6d524da1b098bdf514cda161d41698b488a0956`.

These are package hashes. The manifest's executable hash identifies the offscreen test fixture, not the packaged application.

## Linux inventory

The read-only inventory in job 97369 completed within its 540-second budget; metadata and size records were present for every transaction entry. The three apt simulations are separate, overlapping proposals, not installations:

| Group | Candidate simulation | Installed-version constrained simulation |
| --- | --- | --- |
| mpv runtime | 77 new packages, no upgrades | Candidate transaction already preserves installed versions. |
| mpv development | 150 new packages, 8 upgrades | Failed: `libgbm-dev` and `libpciaccess-dev` require newer installed runtime versions. |
| Display | 3 new packages, 5 upgrades | Failed: candidate `xvfb` requires newer `xserver-common`. |

For the runtime group, summed package metadata reported 58,192,150 download bytes and 164,030,464 installed-size bytes. These are package totals, not actual download-cache usage or incremental disk change. The groups overlap and their totals must not be added. System MpvQt and compatible Qt remain separate prerequisites.

## macOS and runtime limits

The macOS inventory reported no Qt or mpv development inputs in the checked prefixes, and the authorized `runner` account (UID 501) had no existing GUI session; the console UID was 0. This is an observation from the pipeline, not a current host-state guarantee.

The Windows success statuses do not establish recovery or product acceptance. Render evidence was deliberately held while the fixture's render-thread contract is corrected. The separate [Qt-only experiment](windows-2026-11-04.md) and [mpv render-thread experiment](mpv-thread-2026-11-04.md) describe diagnostic observations and their limits; they do not change this pipeline's result.
