# Jellyfin Web producer artifact: offline evidence

The proposed recovery artifact at integration commit
`471172af8e1ae674aa3e88329243975c4438da86` targets upstream Jellyfin Web
revision `ea2abad3e1671473d352b3ccf06f616c61ec9381`. The pristine manager
input was 158,528 bytes with SHA-256
`43a9837e11041ddd624a6f68ee3fe3ffb7ee48dfa1443b27592a67c0c0895af2`.

With `JELLYFIN_WEB_SOURCE` set to those verified bytes, the coordinator ran
`node --test tests/test_jellyfin_recovery_transaction.mjs tests/test_jellyfin_recovery_manager.cjs`:
35 passed, none failed or skipped. The tests exercise the actual patched manager
and transaction logic with fake I/O, including rejection of a provisional
endpoint for live media, unsupported live capability and null candidates.
This is offline boundary evidence, not native execution evidence.

The artifact and this report do not establish runtime resource integration,
a hosted fork, native correlation, or settlement. The existing headless Linux
and macOS gates remain in place; these new artifact tests are not registered in
CI and require a pinned-input bootstrap before they can be registered.
The last native build evidence at `3e3028b` remains unchanged by this artifact
and documentation; it does not validate the new producer artifact.
