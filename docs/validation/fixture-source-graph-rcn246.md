# Fixture copied-source graph proposal

This proposal is based on source commit `b90137b0785c9a30445fd861f1b75fabe89316ac`. It does not change source pins or establish a working fixture. The fixture precondition remains diagnostic-only, static MpvQt, external events OFF, C2 OFF; the recovery runtime definition is not enabled for its `FixtureMpvQt` target.

The unconditional `src/player/CMakeLists.txt` entries selected for `FixtureCore` are:

- `PlayerComponent.cpp`, `PlayerComponentCommands.cpp`, `PlayerIntentCommands.cpp`, `PlayerComponent.h`, `PlayerMpvEvents.cpp`, `PlayerReleasedState.cpp`, `PlayerRecoveryIntegration.cpp`.
- `PlaybackIntent.cpp`, `PlaybackIntent.h`, `PlaybackCheckpoint.cpp`, `PlaybackCheckpoint.h`, `RecoveryIdentity.cpp`, `RecoveryIdentity.h`, `PlaybackNativeReceipts.cpp`, `PlaybackNativeReceipts.h`, `PlayerNativeReceipts.cpp`.
- `NativePlaybackObservation.cpp`, `NativePlaybackObservation.h`, `PlayerNativeObservation.cpp`, `PlayerNativeRestore.cpp`, `AlbumArtProvider.cpp`, `AlbumArtProvider.h`, `MpvVideoItem.cpp`, `MpvVideoItem.h`, `OpenGLDetect.cpp`, `OpenGLDetect.h`, `QtHelper.h`.

`src/CMakeLists.txt` maps each selected player source to `_fixture_copy/player/<name>` before constructing `FixtureCore`. The existing copy list also includes `PlayerIntentObservation.h`, `WindowsLoaderMode.h`, and `WindowsMpvLoader.cpp` for diagnostic includes and loader selection; these are not removed. Other non-player sources follow the existing target mapping and links. `FixtureMpvQt` remains the separate copied target and does not acquire the production recovery runtime sources or compile definitions.

Three unconditional entries are missing from `_fixture_player_inputs`. The following are proposed LF-normalized SHA-256 input values, for review only:

| File | SHA-256 |
| --- | --- |
| `PlayerRecoveryIntegration.cpp` | `a47c259c7ac9e19ba379640ce4937172f7a353e198feb5948200e1809532fad8` |
| `RecoveryIdentity.cpp` | `3407a65cb77b1f4692634bc96a304421a55a84c03dea98ca42b84f621b047097` |
| `RecoveryIdentity.h` | `bf8c8fb6b25e84b14cce770c14775b59ddbb90c219455709e64f35d4fdc36ea4` |

`PlayerRecoveryIntegration.cpp` supplies unconditional `PlayerComponent` methods, with the native recovery implementation behind `MPVQT_RECOVERY_RUNTIME`. The three current fixture native receipt, observation, and restore files include `RecoveryIdentity.h` and use its implementation. The existing mapping includes the `.cpp` entries in `FixtureCore` when the copy list supplies them; no additional direct source entry is proposed. The header is needed by the copied files.

The `AutomaticRecovery*`, `Ordinary*`, `PlayerComponentRecovery*`, and `PlayerComponentOrdinarySelection.cpp` entries are inside the static-plus-external-events condition in `src/player/CMakeLists.txt`; that condition is false under the fixture precondition. No explicit filter is needed or proposed for this target. This observation does not qualify the external-events-ON healthy diagnostic graph.

The current input hashes of eleven already-pinned player files differ from the old copy manifest: `MpvVideoItem.cpp`, `MpvVideoItem.h`, `PlayerComponent.cpp`, `PlayerComponent.h`, `PlayerComponentCommands.cpp`, `PlayerIntentCommands.cpp`, `PlayerMpvEvents.cpp`, `PlayerNativeObservation.cpp`, `PlayerNativeReceipts.cpp`, `PlayerNativeRestore.cpp`, and `PlayerReleasedState.cpp`. Their current source and patched postimages require review before any input or output hash update. The fixture patch hash also remains held pending review. Existing `QFAIL`, source checks, and all output assertions remain unchanged.
