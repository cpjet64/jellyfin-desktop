# Native receipt source checkpoint

At integration source `e97f1c3e3216e5d7ec851606b65dbba872e42b44`, the local
Windows checks below completed with the source unchanged. They do not qualify
native playback, a live restart, or cross-process recovery.

- The patched producer's subscription cleanup, early settlement and synchronous
  error cases passed 44/44 at `89ddbd9c1b1c8abbb05a221eaacf0fd186b89eda` and
  cumulatively at `3824cc0685cc51ea2847ab357cb07bdd9c44f2a5` using verified
  cached input offline (`deps/w788-proof/producer-tests.log`).
- Producer provisioning ran 11 tests with one OS-level symlink test skipped;
  deterministic link and hard-link rejection passed. Routing passed 14 and 12
  tests and the Windows smoke gate passed at `3824cc0`. These are local checks,
  not Linux or macOS execution.
- At `e97f1c3`, Windows static ON/ON passed 38/38 and system OFF/OFF passed
  35/35 (`deps/w789-proof/`). The application SHA-256 values were
  `41fc41b93e9d0dc1e4f2833240ca823ce81b823617809da68c546f9b40f68a6f`
  and `78260c061821d53a01e69e4a6178d617a10b0ab96251c5eba469300fa0ec9afd`,
  respectively. OFF checks establish unavailable behavior only. The Player
  receipt tests inject private mpv events; they do not prove real `mpv_wait_event`.
- Eight fixture-copy tests passed; a fresh Debug held fixture compiled without
  being executed. Its dependency audit found 43 FixtureCore records with no
  original player source/header (`deps/w790-proof/`). The fixture SHA-256 was
  `927ca4e6959c6da991ef993be5b2656faf8cef43cc0825fa2c590b85ece59277`.
- Fifteen fixture/package checks passed at `e97f1c3`: runtime 2, installer 2,
  bundle 4 and freshness 7 (`deps/w792-proof/`). These use fake payloads and
  are not an actual product package.

The native receipt key comprises host, core, revision and attempt. Late
invalidation settlement and nested dispatch were corrected. A candidate requires
a retained receipt and private keyed fixture observation; same-process consume
requires a canonical candidate and burns it. This does not implement restored-load
dispatch, current-intent checking before a future B dispatch, a native position
producer, or cross-process resume. Live capability remains unsupported and its
candidate is null.

The finite healthy-restart plan at `618d26adf6b78a78863b41cfa1398332793923b9`
is proposed and unexecuted. Pipeline 32684 failed; no new CI result is claimed.
Task worktrees were removed after integration; their branches were retained.
