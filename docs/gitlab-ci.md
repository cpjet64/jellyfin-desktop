# GitLab Windows candidate pipeline

This pipeline validates the Windows x64 candidate using the shell executor on
the `win203-unprotected` runner. It builds with MSVC, Qt 6.9.3 and the same
primary and fallback mpv versions as the existing GitHub Windows workflow. It
runs CTest, the Windows mpv loader harness and packaging fixture, then creates the installer and
portable ZIP. Artifacts include CTest's JUnit report, SHA-256 checksums and
pipeline/job/commit provenance. The packages are unsigned and are not releases.

Branch pushes, merge requests, and web/API branch pipelines are admitted.
Push pipelines are suppressed when an MR pipeline covers the same branch.
Protected refs, tags, schedules and other pipeline sources are excluded pending
an explicit protected-resource policy. No caches, signing, deployment, package
registry or release operations are configured. The GitHub workflows remain in
place, including Linux and macOS; this is deliberately Windows-only coverage.

## Migration scope and parity

The requested replacement is the Windows candidate validation run. It is a
deliberate exception to a full multi-platform CI migration: other GitHub
workflows remain active and are not claimed to have been migrated or proven by
this pipeline. No GitHub workflow is retired until the replacement has a green
GitLab run for the delivered commit.

| Behavior | Existing GitHub Windows workflow | GitLab candidate |
| --- | --- | --- |
| Admission | Reusable workflow, manual dispatch, or push to `master`; the parent build also calls it on pushes to `release`, `prerelease`, and `test` and manual dispatch | Unprotected branch pushes, merge requests, and web/API branch runs; duplicate branch push suppressed for an open MR |
| Platform | Hosted Windows x64; ARM64 disabled | Shell executor on `win203-unprotected`, Windows x64 |
| Inputs | Recursive submodules, MSVC, Qt 6.9.3, primary and fallback mpv, VC runtime and Inno | Same product inputs; host tools are prerequisites and dependencies are installed in the checkout |
| Validation | Build, CTest and mpv loader harness, then package | Same validation plus the Windows packaging fixture before packaging |
| Outputs | Installer and portable ZIP | Installer and portable ZIP, JUnit report, checksums and provenance |
| Privileged actions | No signing or release in the Windows build workflow | No signing, release, deployment or registry writes |

GitHub's separate Linux test/coverage workflow and Linux, macOS, AppImage,
coverage-comment and aggregate build workflows remain outside this candidate's
scope. Protected refs are intentionally excluded while the protected-resource
policy is unresolved; they do not receive replacement validation from this file.

The runner must have Python with venv, Visual Studio 2022 x64 C++ tools and
redistributable, CMake with JUnit-capable CTest, Ninja, 7-Zip, and Inno Setup 6.
It needs network access for the recursive submodule, pinned aqtinstall 3.3.0,
Qt modules, the existing SourceForge mpv archives, and Microsoft's VC runtime
installer. Dependencies are installed under the job checkout and require at
least 20 GiB free on its volume before download. This threshold is a preflight,
not a guarantee against concurrent disk use. Inno and other runner tools must
already be installed; the job does not modify the host installation.

Desktop end-to-end tests require an interactive desktop and are intentionally
not run on the headless runner. A green job does not prove interactive playback,
clean-machine installation, signing, or release readiness. The runner's actual
tool availability, project assignment, effective GitLab configuration, and
successful artifacts must be established by a pipeline for the delivered SHA.
