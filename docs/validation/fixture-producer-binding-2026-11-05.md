# Fixture producer binding — Windows build evidence, 2026-11-05

The isolated fixture producer binding built successfully and its corrected
dependency graph was independently reviewed. This is source and build evidence,
not a native fixture or playback run.

## Build and graph

- W677 at `e0ffd39cb57ae7f0f4394743f9eb86a49b0cfaa6` and W682 at
  `e82790ed46276ce61d76c6b3de7d74551f911f05` passed their local builds, but
  their original graph still selected an original `PlayerComponent.h` in four
  fixture-core translation units. A successful compile did not establish the
  intended copied-header provenance.
- The explicit copied-header path correction landed at
  `fe2720868b167cf3531eb9e35986f48bc8c853b6`. W686 rebuilt the fixture only at
  that exact start and end revision; production inputs were unchanged since W682.
  The held fixture executable SHA-256 is
  `f853168a16dc6ab3fef113214bcc10287e5be23292db5c302eaa9852503b37b3`.
- The independent W686 graph review examined 48 fixture-related object records:
  13 selected the copied `PlayerComponent.h`, and none selected its original.
  All four corrected translation units selected the copied header. No original
  MpvQt source path appeared in the fixture records. MOC selected copied
  `PlayerComponent`, `MpvAbstractItem`, and `MpvController` headers; the renderer
  wrapper selected the copied renderer, and the link selected `FixtureCore` and
  `FixtureMpvQt`, with no ordinary counterparts.
- W686 fixture-build log SHA-256:
  `612d24784f7b1d0e4d48e491e1893b178ea9a5caf0fb8dcf31161b523f0e3e54`;
  dependency log: `96dad9ed58aff2e38e64b57c69f5a915ffc85d3f6ff144036099c05fd78cc092`;
  command log: `a5eb18502ce8f8d853cc5c2b64eee7986ead93ed36460363055b161616ffe3ef`;
  raw copied-output hash manifest:
  `94a028f7258f222795546ddc55dc5df755d9fb7b6e972b2f283db66ff53538d1`.

The source generator [pins the complete normalized input and output set](../../CMakeModules/FixtureProducerCopy.cmake)
and the patch SHA-256 `51470cdf3beaffe2e5458dcccfa28016633ad21c35fb5947d3142ecb2a01d1b0`.
The four repaired translation units have these normalized input and output pins:

| Translation unit | Input SHA-256 | Output SHA-256 |
| --- | --- | --- |
| `ComponentManager.cpp` | `e69695ac1e88b8044793ccd5245c61a156f483db5d30c74ef284627f701eae66` | `c9a15173b349ce7b54f97d9e6a42188b5e4a337128bf4503f2c718a266ed0725` |
| `WindowManager.cpp` | `d89e641bad2aa01bf0e2e2855e345a1b155ba73efbe32e30ba708747c1073ae7` | `448aa4969ba5a0b5808ebbee82dc83543c6e0a9578e00145b791af49d664cf91` |
| `InputComponent.cpp` | `1d8814277c3b901455cfcf25fc31400c78b4be8bcde9e36c09bc624ac7a9b823` | `f9eb0ed51f3fb3e841d8afda27bc6d950a2d30020297fc74d815f7a0585165dd` |
| `PowerComponent.cpp` | `0d1c354a121addab787d2390c586b1b762754534eee98f168a39aff854c5b3f5` | `b9f34dc28a2e3ff0b475cdfbbb8840284a8c80012458a0854079ae8cd9fccdda` |

## Local gates and limits

At W682, Release passed 27/27 selected tests and Debug passed 3/3 QtCore tests.
The local Linux synthetic model, adapter and fake-bus suites passed 20, 4 and
10 tests; candidate and observation suites passed 11 and 10 tests. Fixture-copy
checks passed 6 tests, routing and native-order checks passed 12 each, and the
Windows smoke gate passed. At W686, settlement-failure tests separately passed
31/31 at `fe2720868b167cf3531eb9e35986f48bc8c853b6`. These are Windows-hosted
local checks, not native Linux or macOS execution.

The held fixture was not executed. Native callback-barrier behavior, render
retirement, destruction, and retention of ownership through uncertain outcomes
remain unqualified. No native Linux or macOS fixture run is claimed.
