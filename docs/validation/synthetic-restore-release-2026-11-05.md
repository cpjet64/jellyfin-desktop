# Synthetic restore Release diagnostic, 2026-11-05

One Windows Release synthetic restore diagnostic passed. The test injected a synthetic render failure, retired the first render generation, created a second generation, restored a paused frame and resumed playback. This is one result from a controlled fixture; it does not establish stability across runs, recovery from genuine GPU loss, or production recovery readiness.

The fixture was built from `b355a0b555aca9fc2ee72baa5c8bf4533bb288e0` and executed with orchestration source `fe4e4f0203590fab30587301d991b4f8a9ceaed1`. The Release executable SHA-256 was `8e9a2ca555e15a731cdeb3eb346c5f1d14d91058ea8eeb0486a88dae1e8c73fc`. The frozen launch plan was independently checked before launch as `077f7b64c2c77be095db4a88cbf729d0317efc20a9ceba0ea631542bdc6ce306`; the runner receipt does not itself bind that plan hash.

The runner receipt reports `passed: true`. QtTest reports 3 passed, 0 failed and 0 skipped. The hidden desktop helper reports application exit 0, helper exit 0, zero active job processes before and after its check, and no termination request or forced cleanup. The fixture's typed-fault and decoded-frame checks are source-level gates; the runner separately checks PNG signatures and motion-file inequality.

The mutex-protected service observation records successful synthetic activation for generation 1, with the original context, error `-18`, and activation before the recorded deadline. It records four allow evaluations and at least one rejection; the first and last rejection codes are both 2 (`Phase`). This does not mean all four evaluations were rejected. The separately sampled worker atomics record four service entries, one completed update (counted after return) with raw mask 1 (`FRAME`), one frame and zero no-frame updates, and one skip returning 0. These individual atomic reads are **sequential and not a coherent snapshot**; their counts and timestamps cannot establish a single simultaneous state.

The recorded first-generation retirement freed and acknowledged generation 1 before generation 2 was created on the recorded owning render thread and its creation acknowledgement was observed on the GUI thread. The restored position and decoded frame both report 1.9 seconds, within the fixture's 0.1-second tolerance. The paused image could be a retained texture; it does not independently prove a fresh decoded G2 frame. The resumed observation records advancement from 1900 to 2133 ms and two later frames. Final cleanup records two retired and freed generations, observer closure, worker join and surface destruction.

An earlier Release attempt, W528, remains a failure: its retained quiescence observation reports a `service-wait` timeout and the runner reports failure. This later pass does not establish the cause of that earlier failure or erase it. The earlier evidence remains under the private `w524-release-proof` directory.

The W547 private evidence is retained under `deps/w547-release-proof/release-synthetic-restore` in the integration worktree. Its SHA-256 identities are:

| Evidence | SHA-256 |
| --- | --- |
| Runner receipt | `e88e4776c90fc229548b9bb25626ca53840caddf4710934767fc01fd84a87e3b` |
| QtTest log | `7861d2d801472d0a96c8a055601dc28f78cb0547a3db8667460e34e13cd1a41d` |
| Helper stdout | `4f2c1bf03d35cf5cb2b5830df06890d29fa6c2dfb6bb6bcaa702f302901f2c53` |
| Quiescence observation | `641174cd5d6b459166862b882578df3e78eb39bf05fda9be4a6584ca86dfd696` |
| G1 retirement | `25bf5dafc7d98127a9ea8c6a8272462b6ac0e6923a6804c7a23ba3ac577653d7` |
| G2 creation | `18626dd0f947c9ea083e1de822a2688c4862bb7beeaa57562e2bfe37389e0eea` |
| Restored position | `c3a6341c4616dba18ef3e02558c1fb8f1bbd14f24f157313de39eef6b7f5c8b7` |
| Resumed observation | `3aaf85f84b458b7ec68a0e93e0bc27227df43907a64aff0495d86e10f5696e9f` |
| Cleanup observation | `97ae2fa25753500627f62d657dc6f7d1f12cb4fc410e2ac45d6a1391e676db94` |
| G1 frame 1 | `bf43990de9abb4ca30f802a3ee35cc3d54c97578243dcdac4e1199ef9833b4aa` |
| G1 frame 2 | `88a0fdaa0bb485ad1b3a6ee9d5c696caffbe92db548d1e92efa4071a5d41d8eb` |
| Restored paused frame | `9698d4a8dccbfed12a921e7494ef36441129f53d7345185d4a7c81607aa8d1b7` |
| Resumed frame 1 | `a4a18e1e135106f6ffe84b98c88ddfce98135d69828c2e9d9936156b2947596b` |
| Resumed frame 2 | `cb6d175b9faa1bc70bc2c54463393289d9fb715bcaee70dd371edaf98b0423ef` |
