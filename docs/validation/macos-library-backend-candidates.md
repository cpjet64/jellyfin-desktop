# Candidate offline Python backend inputs

These are **proposed additions**, not approved inputs or a verified complete
backend closure. The pinned VapourSynth formula does not itself list these
Python build resources. Its upstream R82 metadata at commit
`e0fadcc1c8977e99b11ce2e478996a867a939874` declares Cython >=3.3.0 and
meson-python; compare that metadata with the hash-pinned release sdist after
separate source acquisition. PyPI JSON metadata observed 2026-11-04 supplies
the exact candidate sdists and published hashes below. No sdists were downloaded.

| Candidate | Exact source | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| Cython 3.3.1 | https://files.pythonhosted.org/packages/99/ca/18e324972e5cac914816fdd7bb03e19a455b90039c277a538f7781b50c85/cython-3.3.1.tar.gz | 3743354 | `e0e45989e821bef6aa9ae15c7d583ab5e055f7406c4b3624e49e06d24e90375a` |
| meson-python 0.22.1 | https://files.pythonhosted.org/packages/b4/40/343ae23722d5d66a7b94b752d1b194202640995296379333b274b1860871/meson_python-0.22.1.tar.gz | 121887 | `52c88628b0e5671592dc2306613fb5f6f3615fd24def059b9894f143b7f9a139` |
| packaging 26.3 | https://files.pythonhosted.org/packages/7d/fa/3944b40b07da9ce895c0e6303a5ab7d53da063554f534556b134a54d6093/packaging-26.3.tar.gz | 313412 | `94edc256424af38762eb31306eed28beb9f0efc50a8837492c9d6fd6004aed79` |
| pyproject-metadata 0.12.1 | https://files.pythonhosted.org/packages/4f/76/1cae539918a7b1746d624c2f01560b793c22cd8c081157505bb9bbf0e34d/pyproject_metadata-0.12.1.tar.gz | 48457 | `8809a4df6fe08279b39a8890669506ed3158e0617855ac9aff098fcbe772ae4c` |
| flit-core 3.12.0 | https://files.pythonhosted.org/packages/69/59/b6fc2188dfc7ea4f936cd12b49d707f66a1cb7a1d2c16172963534db741b/flit_core-3.12.0.tar.gz | 53690 | `18f63100d6f94385c6ed57a72073443e1a71a4acb4339491615d0f16d6ff01b2` |

On Python >=3.12, published meson-python 0.22.1 runtime metadata requires
Meson >=1.2.3, packaging >=23.2 and pyproject-metadata >=0.9.0; the last
requires packaging >=23.2. Python 3.15 and the proposed Meson 1.12.1 satisfy
those stated version ranges, but execution is unverified. The candidate
flit-core 3.12.0 meets the `<4` metadata bounds observed for Jinja 3.1.6 and
pip 26.2.2. This does not prove it can replace the formula-pinned flit-core
4.1.0 in Homebrew's bootstrap or that an override preserves behavior.

The following upstream version tags resolved on 2026-11-04 to the commits
shown; their source metadata has not been compared to the PyPI sdists:

- Cython 3.3.1, [`59e1b91224c973f5e32a49fe802253b0e93b896a`](https://github.com/cython/cython/blob/59e1b91224c973f5e32a49fe802253b0e93b896a/pyproject.toml): build requires setuptools.
- meson-python 0.22.1, [`4771b36414f38a772fcf7364ef7f191f1dd5588b`](https://github.com/mesonbuild/meson-python/blob/4771b36414f38a772fcf7364ef7f191f1dd5588b/pyproject.toml): in-tree `mesonpy` backend with the runtime requirements above.
- flit-core 3.12.0, [`1c8141722066ffe1497e511982feeea0f091ddb6`](https://github.com/pypa/flit/blob/1c8141722066ffe1497e511982feeea0f091ddb6/flit_core/pyproject.toml): in-tree backend with no declared build requirements.
- packaging 26.3, [`929fd4b1410ac7ef61ef3f45b2f5d7e87711a9b5`](https://github.com/pypa/packaging/blob/929fd4b1410ac7ef61ef3f45b2f5d7e87711a9b5/pyproject.toml): build requires flit-core >=3.12.
- pyproject-metadata 0.12.1, [`737644a1482a750ce8a38927b69bbbe07d4e8b65`](https://github.com/pypa/pyproject-metadata/blob/737644a1482a750ce8a38927b69bbbe07d4e8b65/pyproject.toml): build requires flit-core >=3.11.
- glad2 2.0.8, [`73db193f853e2ee079bf3ca8a64aa2eaf6459043`](https://github.com/Dav1dde/glad/blob/73db193f853e2ee079bf3ca8a64aa2eaf6459043/pyproject.toml): build requires setuptools >=61.

One candidate ordering is in-tree flit-core 3.12, then packaging and
pyproject-metadata, staged Meson and meson-python, and separately setuptools
before Cython and glad2. Isolate this from any flit-core 4.1 reference bootstrap.
This graph follows declared metadata only: source archive identity, packaging
bootstrap, generated wheels, dynamic build hooks and Python 3.15 compatibility
are still unverified. In particular, do not silently replace the Python formula's
flit-core 4.1 with 3.12; that is a deliberate proposed difference for review.

The proposed five compressed source sizes total 4,280,800 bytes. Expanded,
wheel, native-extension and peak sizes are unknown. Resolve each candidate's
own immutable source metadata and build backend, dynamic PEP 517 requirements,
bootstrap ordering, Python 3.15 ABI and installed runtime imports before
claiming closure. Compare the actual release archives to the reviewed upstream
metadata; reject unexpected build hooks or downloads. These source additions
and any backend substitution need review alongside licensing and acquisition
scope. The separate toolchain and feature gates remain in force.
