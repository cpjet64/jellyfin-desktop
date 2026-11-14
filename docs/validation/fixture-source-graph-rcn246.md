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

`PlayerRecoveryIntegration.cpp` supplies unconditional `PlayerComponent` methods, with the native recovery implementation behind `MPVQT_RECOVERY_RUNTIME`. The three current fixture native receipt, observation, and restore files include `RecoveryIdentity.h`, and `RecoveryIdentity.cpp` remains an unconditional selected input. In the original external-events-OFF, recovery-runtime-OFF mode, the compiled native fixture operations are fail-closed stubs; this source graph does not prove native operation behavior. The existing mapping includes the `.cpp` entries in `FixtureCore` when the copy list supplies them; no additional direct source entry is proposed. The header is needed by the copied files.

The `AutomaticRecovery*`, `Ordinary*`, `PlayerComponentRecovery*`, and `PlayerComponentOrdinarySelection.cpp` entries are inside the static-plus-external-events condition in `src/player/CMakeLists.txt`; that condition is false under the fixture precondition. No explicit filter is needed or proposed for this target. This observation does not qualify the external-events-ON healthy diagnostic graph.

The current input hashes of eleven already-pinned player files differ from the old copy manifest: `MpvVideoItem.cpp`, `MpvVideoItem.h`, `PlayerComponent.cpp`, `PlayerComponent.h`, `PlayerComponentCommands.cpp`, `PlayerIntentCommands.cpp`, `PlayerMpvEvents.cpp`, `PlayerNativeObservation.cpp`, `PlayerNativeReceipts.cpp`, `PlayerNativeRestore.cpp`, and `PlayerReleasedState.cpp`. Their current source and patched postimages require review before any input or output hash update. The fixture patch hash also remains held pending review. Existing `QFAIL`, source checks, and all output assertions remain unchanged.

The exact LF-normalized input comparison is:

| File | Existing pin | Current input |
| --- | --- | --- |
| `MpvVideoItem.cpp` | `5d738fb243f8e3d23e245d78965c6440b96f32d1e78cb405e77c341b99575b06` | `dc6d751e22098e01ce23c063236c40f58901f0dbf556ff136b0d5400453199f9` |
| `MpvVideoItem.h` | `1e1bba8c18f5548535bc8e71da1259457a081cab8cda3dd8957c10852b8b8ab2` | `67173fe89202b1a2fa560a72c0b8ac5e9c63465467e00ee213490f7a7e415741` |
| `PlayerComponent.cpp` | `febe98eb5babf108f5ad53542ad99b3b3e207ada13a2f36432235f9692cc2b7c` | `09e1fba841e1f5ce65ffe9f7666def01a7382e2c8d85f9e7ba32748bb26ec682` |
| `PlayerComponent.h` | `047877c4c057926643a89356b2ee0facd7898ec29d06f839460139d29705d75b` | `ff98236822a7fb029f5d19c5337e97ce77a276fbc251e1d7441fde7043ebdfa2` |
| `PlayerComponentCommands.cpp` | `407656d9a30cae0e9c1380e80c79903ade593c8bfba53e2e59c62cf06e987825` | `7dc9886615f399e505b00c9fd3c8ab9b7fe9a09583df7584a8979efb0182661b` |
| `PlayerIntentCommands.cpp` | `36a8572d536a22e5aa35caa7a8d3a8c736c5b7471535c209232a1dfb986f4e9b` | `86c04522d0edb7f1ec35cd16ea8dd4bef7b66c57ad38808a42307b556470d032` |
| `PlayerNativeReceipts.cpp` | `faad896cf062840fe61e019285989ac25ab3ad0d258541c2c694d8a9ee1139ef` | `5657cca4863686074eeb27ca6e6a196d6e38a9bfc7ac4a98b00d7ef7135e9c0d` |
| `PlayerNativeObservation.cpp` | `6784a8284ba18ca864316c2ccba9e8ee90c8e4f7dcdc1e499e8f5446b511ceb3` | `c6b7bf62509c47e71a35e90c8f72ea29414f9df96bc4dac6d09964a1b5b597f7` |
| `PlayerNativeRestore.cpp` | `ed3f1a9aac0ca6b2c00795f5908f44e2213de7a9fa8548dd32db7c3d27052ccb` | `2c563be21933c4f2134abd98afc6de1427536685d1caa480640e17d8599bf23d` |
| `PlayerMpvEvents.cpp` | `3d2c55d910485978242fa2348d54f28ef7a74cd299073ab6843aaa1b2dbd0d55` | `7b5a7bb57507048883b78788c533572084339839486754b95778b3e9502bc4b3` |
| `PlayerReleasedState.cpp` | `82a125b3629891080ec4e051841ee309d7c967374c66532a6928e3664437f62e` | `2b08dafb06e97d5dc6e99cd9e8941a0a969660134d72545a32baabb83c7b9360` |
