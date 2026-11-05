# Playback intent foundation — 2026-11-05

This records local source and offline/headless checks at
`70fee1a6af02c72cf99dcd004c5902e217d26274`. It is foundation evidence, not
automatic playback recovery or full milestone acceptance.

The source changes are native intent tracking at
`ab04afe7798423d1d4496bdc133a49c692635838`, local Web adapter ordering at
`eeceb3b92e60e49f24a06d2ff55e9a3a471f6ff0`, the producer investigation at
`5d55501ff51a9836bce2a74c5801a1d71f8fb45d`, and checkpoint, tests and fixture
copy pins at `70fee1a6af02c72cf99dcd004c5902e217d26274`.

The private in-memory candidate is constructed from the actual native intent
state. Its checkpoint uses a bounded allowlist and leaves unavailable logical
identities, source position, tracks and queue association unknown. No current
producer identity or request-correlated playback observation is established;
automatic resume remains ineligible. Syntax restrictions on logical IDs do
not establish that arbitrary strings are nonsecret; a future producer boundary
must establish their semantic provenance. The candidate is not a saved resume
ticket and does not issue server requests or relaunch playback.

## Local checks

- Windows Release: 30 selected tests passed. Debug: six selected tests passed,
  including the actual PlayerComponent intent boundary. These are local
  headless checks, not native playback or GPU-reset tests. The Release
  executable SHA-256 was
  `117a5b7e6e69e2dbd776c6138eba2347c86dadda0800af9afb3fa1aa8283c84f`.
- The diagnostic fixture compiled. Its copy check passed six tests; the
  dependency audit covered 51 relevant object records, with 14 copied Player,
  16 copied intent and two copied checkpoint header references, and zero
  original-header references in those categories. MOC and link checks selected
  the intended copied graph.
- The local JavaScript player suite passed 34 tests and input suite four. The
  Linux offline suite passed 40 tests; the macOS protocol suite passed 54.
  Headless routing and native-order checks passed 12 each, and the Windows
  smoke-gate check passed. These are portable/offline results, not native
  Linux or macOS platform execution.
- Four bundle-path tests and seven package-freshness tests passed with fake
  package payloads. They do not validate the new JavaScript resources in an
  actual distributable.

The Windows Release check used `cmake --build <release> --parallel 2` and
`ctest --test-dir <release> -LE gui --no-tests=error --output-on-failure`.
The Debug check built the six selected targets with `cmake --build <debug>
--target test_healthy_wake_binding test_healthy_cleanup_coordinator
test_synchronous_stop_events test_playback_intent_acceptance
test_playback_checkpoint test_player_intent_boundary --parallel 2`, then ran
those same names through `ctest --test-dir <debug> -R <six-name-regex>
--no-tests=error --output-on-failure`. The diagnostic fixture build targeted
`test_webclient_containment` without launching it. The copied source test used
`python -B tests/test_fixture_producer_copy.py`. The local build and test
records identify the same starting and ending source SHA.

The portable checks used `node --test tests/test_inputPlugin.js` and
`node --test` with `tests/test_mpvVideoPlayer.js`,
`tests/test_mpvVideoPlayer_order.js`, `tests/test_synchronous_stop_page.js` and
`tests/test_playback_intent_acceptance.js`. Python checks used
`python -B -m unittest discover -s dev/ci -p 'test_linux_synthetic_service*.py'`
with the verified wheel supplied, the equivalent `test_settlement*.py` discovery,
and `python -B` on `dev/ci/test_native_order.py` and
`dev/ci/test_headless_routing.py`. The smoke check used
`pwsh -NoLogo -NoProfile -NonInteractive -File dev/windows/test_ci_smoke_gate.ps1`.
The package fixture checks used `python -B tests/test_bundle_paths.py` and
`python -B tests/test_windows_package_freshness.py`.

The strict producer endpoint is absent. No host-bound producer, replay or
conflict qualification, or end-to-end stale-event suppression is claimed.
Actual quit versus close-to-tray behavior was not exercised. No native Linux or
macOS acceptance, fixture playback, actual graphics reset, exact-SHA CI run,
release, deployment or hands-on user acceptance is established by these checks.
