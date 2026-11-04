# Synthetic fault quiescence on Windows, 2026-11-04

This records the narrowly scoped synthetic fault quiescence experiment with the original OpenGL context still usable. It does not establish recovery from actual context loss, replay, automatic recovery, or the G2 acceptance gate. The earlier active failure remains failed.

The binaries were built from `6ade929374c03fef9de83b3eb5619ea011f6dbd2`; the orchestration source was `778b6eb3a00ae3ca0c42d499bae6299ad78512bd`, with the relevant build inputs unchanged. The service budget was 15 seconds, approved for this experiment.

Both Debug and Release observed one pending service request and one skip, with matching generation, render thread, and context; the service returned zero and recorded no ordinary post-fault render. Each raw Stop record had one entry, one cancellation, and terminal idle with no video output and force-window disabled. In each run, measured terminal confirmation preceded service-close acknowledgement, which preceded render-context free. The recorded retirement also included acknowledgement, worker join, and surface destruction.

Each QtTest footer reported 3 passed, 0 failed, and 0 skipped. Each desktop helper reported an application exit of zero, zero job processes before and after cleanup, and no termination request or forced cleanup. These results apply to the single synthetic case and the recorded Windows builds only.

The bounded [Debug measurement](synthetic-quiescence-2026-11-04/debug.json) and [Release measurement](synthetic-quiescence-2026-11-04/release.json) are byte-identical copies of the originals. They include transient process-local identifiers used to compare the measured context and thread. The full logs remain private; their SHA-256 digests are:

| Debug evidence | SHA-256 |
| --- | --- |
| Measurement JSON | `7ecbbe969286b154985d306018439187c3aab845e19f65b3e338832cbbb6ec0a` |
| QtTest log | `639103cc7140b6a50fd67e74b096b83e018f5a1ea9813b6fd8eac50b9cc492e4` |
| Helper stdout | `496d045340b090f01e531bfe575441ee3d854fb254c903dc256c488746af2f78` |
| Helper stderr, empty | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |

Debug executable SHA-256: `ed2f0caa1a26d79beb80458f1624e073c915e55fa4521a103bd00e5f743a2abb`. Helper SHA-256: `706e0a939b4dff4b2f2996996863943c3d3a5dfba6dc46e96578f4fb73bd7a42`.

| Release evidence | SHA-256 |
| --- | --- |
| Measurement JSON | `70ae5aad8ba30434e7b4d36f3e5c3a2c33992a81eb24eea0d3e0a985fac8e463` |
| QtTest log | `00266e49d8ae32b88a4f1e7dfad5a330bd3216418880759084c5d96526efcc0c` |
| Helper stdout | `a7a12d913a73aa3cd35d40dd9af413f46af031c753831e0ef32e2834213cb282` |
| Helper stderr, empty | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |

Release executable SHA-256: `3474f094e7b78735a07f2ed8c2552e47d6969a598b1f6a36aa9f4061f7fed4b6`. The same helper was used for both runs.
