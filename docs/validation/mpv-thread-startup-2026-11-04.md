# mpv diagnostic startup failures — 2026-11-04

Two isolated Windows diagnostic runs used [source commit `39c3c12166eca23610a5476f168445a84e3ea671`](https://gitlab.curtpme.com/cpjet64/jellyfin-desktop/-/commit/39c3c12166eca23610a5476f168445a84e3ea671). Both Debug and Release builds completed with Ninja, Qt 6.9.3, bundled MpvQt, external events and the diagnostic-only option enabled. Application and package targets were excluded.

| Input or executable | SHA-256 |
| --- | --- |
| Debug fixture | `be9053b214df12eeb548e1ff79a4d0fa646c17fd9a1d6ff9377918ac865a0bf0` |
| Release fixture | `03b7465f8b7b6cbf5b1c95b2280eee404ccb8518e4a936e5edecc9fee4de4228` |
| Generated clip | `be9dc89757c53be878b9bcfb399a6da3146d7b39288323b68941a6689f878ff1` |
| libmpv DLL | `5c5c9d094859bdc60b8b27b2d832db8fa8c2e487efbd35f5aea5afe6960dad39` |

Both fixtures reported the critical diagnostic `Section "albumart" is unknown` before the load. Each subsequently failed the combined assertion at `test_mpv_thread_independence.cpp:142`; that assertion does not identify which individual condition failed. Source inspection shows the fixture constructs `PlayerComponent` before its `prepare()` call initializes settings, and `AlbumArtProvider` reads the `albumart` setting during construction. This is a plausible source of the critical diagnostic, not proof of the combined assertion's individual cause.

Each application exited with code 1, and its hidden-desktop job reported zero remaining processes. The helper's forced-cleanup flag does not establish that it killed a live process after the application had already exited. Neither run produced a complete QTest footer, hold images or JSON, or lifecycle evidence. Neither proves successful G1 rendering or retirement. No unchanged retry was performed.

An earlier wrapper attempt stopped during Debug configuration when PowerShell treated a CMake deprecation warning on stderr as an error. It did not compile or run a fixture; that wrapper failure is distinct from the two runtime failures above.

The [previous render-thread report](mpv-thread-2026-11-04.md) records the earlier `3ff7f6c` Debug and Release observations. This report does not change or extend that evidence, including its limitations at `47e7f0b`.
