# Python build backend metadata

This is a metadata-only review, not a build lock. The reference formulae are
Homebrew core commit `c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5`. The
upstream version-tag links below identify observed source metadata. GitHub's
commit API resolved each tag to the commits listed below on 2026-11-04; the
relationship to each formula's hash-pinned release archive still needs proof.
Generated release archives can contain different metadata or generated files;
no archive was downloaded or compared in this review.

| Component | Observed upstream metadata | Effect on candidate closure |
| --- | --- | --- |
| Meson 1.12.1 | [pyproject.toml, lines 1–3](https://github.com/mesonbuild/meson/blob/1.12.1/pyproject.toml#L1-L3): `setuptools>=42`, backend `setuptools.build_meta`. | Formula uses pip build isolation. The constraint is a minimum, not a version lock; identify the actual backend and wheel/build inputs. |
| setuptools 84.2.1 | [pyproject.toml, lines 1–11 and 27–30](https://github.com/pypa/setuptools/blob/v84.2.1/pyproject.toml#L1-L30): empty build requirements, in-tree `setuptools.build_meta` backend and `backend-path=["."]`; no declared base project dependencies. | Formula resource version satisfies the listed Meson, Mako, Markdown and MarkupSafe minimums, but does not prove compatibility or generated-sdist equivalence. Its optional `core` dependencies are not base project dependencies; audit any selected extras and vendored files. |
| Mako 1.4.3 | [pyproject.toml, lines 1–2 and 25–26](https://github.com/sqlalchemy/mako/blob/rel_1_4_3/pyproject.toml#L1-L26): `setuptools>=77.0.0`, setuptools backend; runtime `MarkupSafe>=2.0`. | GI formula resource MarkupSafe 3.0.4 satisfies the stated runtime range; verify the selected archive and isolated build environment. |
| Markdown 3.11.2 | [pyproject.toml, lines 1–3](https://github.com/Python-Markdown/markdown/blob/3.11.2/pyproject.toml#L1-L3): `setuptools>=77.0`, setuptools backend. | GI resource version is pinned by formula, backend is not pinned by upstream constraint. |
| MarkupSafe 3.0.4 | [pyproject.toml, lines 55–57](https://github.com/pallets/markupsafe/blob/3.0.4/pyproject.toml#L55-L57): `setuptools>=77`, setuptools backend. | Qualify its native extension and resulting x86_64/Python ABI; declared backend alone is not build evidence. |
| Jinja2 3.1.6 | [pyproject.toml, lines 17–18 and 33–35](https://github.com/pallets/jinja/blob/3.1.6/pyproject.toml#L17-L35): `flit_core<4`, backend `flit_core.buildapi`; runtime `MarkupSafe>=2.0`. | libplacebo generator resource needs a separately pinned compatible flit-core; formula Python's flit-core 4.1.0 does not satisfy the declared bound. |
| pip 26.2.2 | [pyproject.toml, lines 45–47](https://github.com/pypa/pip/blob/26.2.2/pyproject.toml#L45-L47): `flit-core>=3.11,<4`, backend `flit_core.buildapi`. | Python formula's flit-core 4.1.0 does not satisfy this declaration. Formula invokes pip wheel with `--no-build-isolation`; whether that actual path succeeds and what compatibility rule it follows is unverified. Do not treat this as a proven failure or silently ignore the mismatch. |

The formula-pinned GI resources also include setuptools 84.2.1 and the three
other resources above; the separate libplacebo formula additionally includes
glad2 2.0.8. Python 3.15.2's formula pins flit-core 4.1.0 and pip 26.2.2.
Formula source URLs and hashes, including resources and patches, are indexed in
[macos-media-resource-index.md](macos-media-resource-index.md). These names do
not settle build-system bootstrap ordering or all transitive backend inputs.

VapourSynth 82's [R82 pyproject metadata](https://raw.githubusercontent.com/vapoursynth/vapoursynth/e0fadcc1c8977e99b11ce2e478996a867a939874/pyproject.toml)
declares `Cython>=3.3.0` and `meson-python` as build requirements. The R82 tag
was separately resolved to commit `e0fadcc1c8977e99b11ce2e478996a867a939874`;
the immutable metadata URL was subsequently retrieved and confirms those
requirements. Neither requirement has an exact version or artifact hash in the
formula's listed resources. The formula stages its pinned glslang source
and installs with `virtualenv_install_with_resources(without: "glslang")`;
that helper's dependency resolution and network behavior remain unverified.

As a bounded example rather than a selection, upstream [meson-python 0.18.0
metadata, lines 4–13 and 34–40](https://github.com/mesonbuild/meson-python/blob/0.18.0/pyproject.toml#L4-L40)
declares, for Python 3.15, Meson >=1.2.3, packaging >=23.2 and
pyproject-metadata >=0.9.0, with an in-tree `mesonpy` backend. Its older
Python-only tomli requirement does not apply to Python 3.15. No evidence
selects 0.18.0 for this candidate; resolve and pin the actual meson-python,
Cython, packaging and pyproject-metadata versions, hashes and their own
build requirements, plus any dynamic backend requirements. The formula's
Meson 1.12.1 meets that example's declared minimum but has not been qualified
as the backend's selected Meson.

[Candidate additions](macos-library-backend-candidates.md) record proposed
Cython 3.3.1, meson-python 0.22.1, packaging 26.3, pyproject-metadata 0.12.1
and flit-core 3.12.0 source hashes; they are not yet an approved closure.
Also unresolved are glad2's backend, the exact flit-core 4.1.0
bootstrap metadata, candidate flit-core 3.x compatibility,
dynamic PEP 517 requirements, Python 3.15 binary compatibility, and any
network or tool invoked by build hooks. Resolve these from immutable source
commit and release metadata; compare archive contents when separately
authorized. No source archive, installation or build was performed.

The resolved upstream tags are Meson `70598bb7825f0f804fb31ec2795de5e79e7692a0`,
setuptools `3895f9829fc0e51882981773a823a658eb1d1636`, Mako
`6b833ae06fc8866b89ea7939e5d36dd71699cf05`, Markdown
`b41e0aaf1181f1a8cb200cf5a815aad7c0020d23`, MarkupSafe
`ece26078ec168ebac69f02877238e68d4b376c91`, Jinja
`15206881c006c79667fe5154fe80c01c65410679` and pip
`b3b696fe2122f2339d782517559bfade2cb6b879`. These identify metadata
snapshots and do not authenticate the separately packaged archives.
