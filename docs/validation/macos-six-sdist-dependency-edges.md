# Offline dependency edges from six retained sdists

This table derives from the retained selected-text JSON, SHA-256
`6566b7571dbd385d710cbceb29cd6768510d4845caedd7847dbbbd6060892189`,
whose archive identities and limits are recorded in the
[six-source observation](macos-six-sdist-observation-2026-11-04.md). Member
hashes below are SHA-256 over the UTF-8 bytes reconstructed from that JSON's
captured text. They identify the selected text, not independently extracted
archive members. Parsing used only the retained text; no new member was read,
and no setup, backend import, hook, Meson or build was executed.

| Archive | Exact retained member and text SHA-256 | Declared dependency edges and limits |
| --- | --- | --- |
| VapourSynth 82 | `vapoursynth-82/pyproject.toml` `0261e1260de6eb74618db50b942c8a0328b9f3cd5c4ee1f9036831b4738d4606`; `vapoursynth-82/meson.build` `a890f6105a35e9db017cf35da5d44219eba8c1f0b1917bce399c538e2f241ba8` | Build requires `Cython>=3.3.0` and `meson-python`, backend `mesonpy`; Python >=3.12, dynamic version. All platforms require threads, zimg >=3.0.5 and the glslang subproject; non-Windows additionally requires `dl` and Python >=3.12, while Windows requires libp2p and Python >=3.14. Vulkan headers fall back to a subproject only when detected headers fail the check. |
| Cython 3.3.1 | `cython-3.3.1/setup.py` `c7fc3e7937fc60abeb59ad83b2a348c63d957f81339bd7d18bb9113315d3fe03`; `cython-3.3.1/PKG-INFO` `48107ce596e5dcb461b1d8cc7f751f98f95bea99b0d0394db1596ffec27ff5d0` | Actual inventory has **no `pyproject.toml`**. Python >=3.9; setup code tries setuptools and falls back to distutils, imports subprocess and conditionally references Cython build modules and `pgen`. The backend/build-isolation behavior and referenced source implementation remain unverified. |
| meson-python 0.22.1 | `meson_python-0.22.1/pyproject.toml` `aba2cd9fe19805c68a9cbf6718e3756e0e21a1eae81cc5adf8d3f2a2ed91659b`; `meson_python-0.22.1/mesonpy/__init__.py` `a6c3009e6e619bf538bccc732861cbaae81bc608d1964617ba0b3cd232908164` | In-tree `mesonpy` backend, Python >=3.10. For Python >=3.12: Meson >=1.2.3; non-iOS: packaging >=23.2; pyproject-metadata >=0.9.0. The Python <3.11 tomli marker does not apply to Python 3.15. Captured hook logic can request Ninja >=1.8.2 when `NINJA` is unset and its command lookup returns none; Linux-only patchelf conditional does not apply to Tahoe. |
| packaging 26.3 | `packaging-26.3/pyproject.toml` `8497e780f8cb36c109fb25dd26e2981086cd5964074e017f03d36bb1f2fe5e9a` | Backend `flit_core.buildapi`, build requires flit_core >=3.12; Python >=3.9, no declared project dependencies, version dynamic. Source implementation was not selected as text. |
| pyproject-metadata 0.12.1 | `pyproject_metadata-0.12.1/pyproject.toml` `1a4e0f3cb839c9641e3a2645b1bf8d030aaff126fd2a3b857f689f402b098f3c` | Backend `flit_core.buildapi`, build requires flit-core >=3.11; Python >=3.8 and runtime packaging >=23.2; version dynamic. Source implementation was not selected as text. |
| flit-core 3.12.0 | `flit_core-3.12.0/pyproject.toml` `badedc67bba738b05f9cfe78c9bcdb0402d0fb132a33fc21362ac392f16599de`; `flit_core-3.12.0/flit_core/buildapi.py` `6b0a2d838397149f8526ce89e5869f04438db24c927fa4c9885d23e2e923538c` | In-tree `flit_core.buildapi` backend with no declared build or project dependencies; Python >=3.6 and version dynamic. Its captured wheel requirement hook inspects dynamic metadata and may return runtime requirements when static version/description inspection is insufficient. This candidate is **not** the reference Python formula's flit-core 4.1 bootstrap. |

## Retained hook evidence and unresolved branches

Short excerpts from the retained implementation, without invoking either hook:

- `meson_python-0.22.1/mesonpy/__init__.py`, lines 1317–1318:
  `if os.environ.get('NINJA') is None and _env_ninja_command() is None:` then
  `dependencies.append(f'ninja >= {_NINJA_REQUIRED_VERSION}')`. The captured
  constant is `1.8.2`. Lines 1324–1333 apply the same test to wheels and add
  patchelf only on Linux. The lookup implementation can run a Ninja
  `--version` subprocess; which command a future isolated environment selects
  is not established by this text.
- `flit_core-3.12.0/flit_core/buildapi.py`, lines 28–37, checks dynamic
  summary/version and AST-derived values, then returns
  `info.metadata.get('requires_dist', [])` if those values are insufficient.
  Actual dynamic requirements and import fallback behavior must be reviewed
  for each selected project rather than inferred from an empty initial list.
- `cython-3.3.1/setup.py`, lines 1–5, tries `from setuptools import setup,
  Extension`, falling back to `distutils.core` on ImportError. Its source also
  imports `subprocess`, conditionally calls `pgen` and references
  `Cython.Distutils.build_ext`. These are source observations, not proof of
  what an unrun build would execute.

The hook files refer to additional **captured** implementation. Their exact
member names and selected-text hashes are:

| Member | SHA-256 |
| --- | --- |
| `meson_python-0.22.1/mesonpy/_rpath.py` | `7809a1005fd95bd4e69b3038b6c854b90fa0fca13eb0ebe45fe981cb5e0c8d8a` |
| `meson_python-0.22.1/mesonpy/_tags.py` | `2323c7dc873b585bacb6e8c2727aa3adfdf74a949c088aa63bec12a11493d3b3` |
| `meson_python-0.22.1/mesonpy/_util.py` | `f32d8b63ae984d8a593fee55f05f35d1610b2fe48b886b113ec1a3d4b4754e3a` |
| `meson_python-0.22.1/mesonpy/_wheelfile.py` | `ac438b8e82834b5a5ca4949ce6514501c7264520994953781d4ab74a532632b2` |
| `flit_core-3.12.0/flit_core/common.py` | `594cfb7c8742b147945bc47c89973282b986cc1d3868525b5c46aade9200a899` |
| `flit_core-3.12.0/flit_core/config.py` | `a92a808360f8e0803f2ea27fa3d2bb46be028b9c961b9629f6cf64d5ee3790f9` |
| `flit_core-3.12.0/flit_core/wheel.py` | `4bdc2622c1329f839de480f217384b827399087dfd64fc5422de44b99e09e3e9` |
| `flit_core-3.12.0/flit_core/sdist.py` | `0eb4596bd193f962ccede30311f74391c0defb8344641284120d616f92703c51` |

VapourSynth's retained wraps are `vapoursynth-82/subprojects/glslang.wrap`
(`ff0fe4aaef3c67d89bba5d52f2130a9c1794a4ccacd0c4a093f49abd5cf9949f`),
`vulkan-headers.wrap` (`f48682c942699f23b864ec9a041ab615c93e26d4fc28076e061d301ae5714777`),
`libp2p.wrap` (`15638aa5a4d89a7c26ca625d3d80cb4a0fe0d097e9ba45e2e5a4e9c12d4bb35a`)
and `zimg.wrap` (`8413163046a9d445fac85b5742bf4b3c8e62b0c169d53d0a5768ee9eb2c01cea`).
The glslang and Vulkan headers wraps name SDK 1.4.357.0; the Homebrew formula
instead stages glslang 1.4.363.0. This is an unresolved build-policy difference,
not permission to override or fetch. The libp2p and zimg wraps name `HEAD`;
both remain unresolved, with libp2p's dependency Windows-only in the captured
top-level source. No fallback download is authorized.

## Exact additional members requiring review

These paths appear in the retained archive inventories but their text was not
selected. They are a bounded next-review list, **not** permission to extract or
execute them; additional references may emerge after review.

- `cython-3.3.1/Cython/Distutils/build_ext.py`
- `cython-3.3.1/Cython/__init__.py`
- `cython-3.3.1/Cython/Compiler/Main.py`
- `cython-3.3.1/Cython/Build/Dependencies.py`
- `cython-3.3.1/Cython/Parser/Grammar`
- `packaging-26.3/src/packaging/__init__.py`
- `packaging-26.3/src/packaging/utils.py`
- `packaging-26.3/src/packaging/version.py`
- `pyproject_metadata-0.12.1/pyproject_metadata/__init__.py`

The Cython setup source names `Cython/Parser/graminit.h` and `graminit.c` as
conditional `pgen` outputs; neither appears in the retained inventory. Their
generation path and whether it applies are unresolved, not missing members to
extract.

Captured mesonpy and flit-core helper modules also reference further internal
implementation; a complete dynamic-hook and offline input closure is not yet
established. This analysis does not change the separately recorded unresolved
process-identity mapping or establish macOS build, runtime or acceptance proof.
