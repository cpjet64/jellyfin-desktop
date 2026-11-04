# Six macOS source archives: bounded static observation

On 2026-11-04, one authorized Windows-hosted acquisition and static inspection
ran against source revision `757f7c52137acca5214ae1f057b54b7c5613e7fd`. The
six exact compressed archives below matched their pinned sizes and SHA-256
values. The run transferred **5,421,756 bytes** and counted **33,966,080
decompressed bytes**, within the 8 MiB transfer and 256 MiB streamed-expansion
limits. The worker reported a verified 480 MiB reservation, removed after
successful postflight. Its retained inspection JSON is 610,315 bytes with
SHA-256 `6566b7571dbd385d710cbceb29cd6768510d4845caedd7847dbbbd6060892189`.
The supervisor receipt's SHA-256 is
`01c645e3c26cb65f6b59455f00d52623c80cc841b4e332d1b54a60a4202e8090`.

| Archive | Bytes | SHA-256 |
| --- | ---: | --- |
| vapoursynth-82.tar.gz | 1,140,956 | `1128316620890ba8e5b69c924a665f98001b2ba4230dadead0f296446ea71843` |
| cython-3.3.1.tar.gz | 3,743,354 | `e0e45989e821bef6aa9ae15c7d583ab5e055f7406c4b3624e49e06d24e90375a` |
| meson_python-0.22.1.tar.gz | 121,887 | `52c88628b0e5671592dc2306613fb5f6f3615fd24def059b9894f143b7f9a139` |
| packaging-26.3.tar.gz | 313,412 | `94edc256424af38762eb31306eed28beb9f0efc50a8837492c9d6fd6004aed79` |
| pyproject_metadata-0.12.1.tar.gz | 48,457 | `8809a4df6fe08279b39a8890669506ed3158e0617855ac9aff098fcbe772ae4c` |
| flit_core-3.12.0.tar.gz | 53,690 | `18f63100d6f94385c6ed57a72073443e1a71a4acb4339491615d0f16d6ff01b2` |

The recorded supervisor reports worker exit zero, reaped true, no forced stop,
bounded output, 1.613 seconds elapsed, 610,315 stdout bytes and zero stderr
bytes. The outer launcher reported exit zero after 1.819 seconds. Its process
query observed a direct child whose identity differed from the worker PID in
the supervisor receipt. A later exact-ID query found all three recorded PIDs
absent. The differing observation is unresolved: this evidence does **not**
prove the identity of the extra observed process or complete descendant
settlement. The raw receipt, inspection JSON, archives, launch logs and
redacted outer observation remain retained as private local evidence.

The full supervisor receipt, with the private PID redacted, is:

```json
{
  "child_pid": "redacted",
  "exit_code": 0,
  "forced_stop": false,
  "child_reaped": true,
  "output_within_limit": true,
  "elapsed_seconds": 1.613,
  "stdout_bytes": 610315,
  "stderr_bytes": 0,
  "inspection_sha256": "6566b7571dbd385d710cbceb29cd6768510d4845caedd7847dbbbd6060892189",
  "archives": 6,
  "transferred": 5421756,
  "expanded": 33966080,
  "worker_sha256": "5848f746a9c911841addd21d36a81a5e05ec9143a8de2116e4165b96ae48fcaf",
  "manifest_sha256": "b613594af22a0c36ca81fb8c2e7f90d4a09ddafd41cb2d8f033e7fd80e8e201c",
  "python_sha256": "7ca24f26d6e3f463419ee4f537ddd3acd312c38fe45e678cce08572f26a8bd1a",
  "reservation_bytes": 503316480,
  "scope": "single reviewed worker; no children"
}
```

The receipt's `scope` describes its assumed worker contract; the unresolved
outer process observation prevents interpreting it as complete tree evidence.

## Observed source metadata

The inspection listed 3,672 member names and retained 173 selected text files
across the six archives. This is a partial static selection, not an exhaustive
review of every build hook or all transitive inputs. No archive member was
extracted to the filesystem, and no setup script, backend import, PEP 517 hook,
pip, Meson or build was run.

- The VapourSynth 82 sdist's root `pyproject.toml` declares `Cython>=3.3.0`
  and `meson-python`, with backend `mesonpy`. The actual sdist metadata
  therefore confirms these requirements independently of the earlier
  upstream-tag observation.
- The Cython 3.3.1 archive inventory contains no `pyproject.toml`. Its root
  `setup.py`, `setup.cfg` and `PKG-INFO` were retained for static inspection.
  The captured `setup.py` references setuptools and Cython build modules;
  referenced implementation files were not all captured as text. Upstream-tag
  metadata cannot be assumed identical to this release sdist.
- The captured meson-python, packaging, pyproject-metadata and flit-core root
  metadata identifies their respective build backends. Their runtime and
  dynamic build-hook requirements still need analysis; flit-core 3.12 remains
  separate from the reference flit-core 4.1 bootstrap.
- VapourSynth includes wraps for glslang and Vulkan-Headers at
  `vulkan-sdk-1.4.357.0`, and libp2p and zimg wraps at `HEAD`. The captured
  top-level `meson.build` declares libp2p in its Windows branch, zimg as a
  dependency, an unconditional glslang subproject, and a conditional Vulkan
  headers fallback. Presence of a wrap does not establish it is used on Tahoe.
  The reference formula stages glslang 1.4.363.0; that difference and all
  allowed offline resolution paths require review before any build.

This observation does not establish a complete Python backend closure, source
buildability, macOS 12 compatibility, an installed media stack, native
rendering, playback, or acceptance. The 89-node build and unresolved Git
archive acquisitions remain held. No Mac guest, global configuration, or
service was changed.
