# Offline Jellyfin Web recovery patch candidate

This is a review artifact for Jellyfin Web commit
`ea2abad3e1671473d352b3ccf06f616c61ec9381`, not a deployed fork or an enabled
Desktop capability. The upstream Web source is GPL-2.0-or-later; the patch and
helper are provided for application to that source under the same license.
`source.json` pins the exact upstream manager bytes and Git blob.
It also pins the LF-normalized patched manager, patch and helper hashes; the
harness normalizes checkout CRLF for those outputs before verification. The
upstream manager input itself is verified as exact raw bytes.

Obtain the pristine file from the pinned upstream revision and independently
verify its 158,528-byte size and SHA-256 in `source.json`. Do not use a live
Web client, URL with credentials, or source from an unknown revision. Keep the
verified copy in task-owned storage. Tests never acquire source or contact a
server; missing or altered source fails before patch application.

From the Desktop repository root, with Node and Git available, set
`JELLYFIN_WEB_SOURCE` to that pristine file and run:

```text
node --test tests/test_jellyfin_recovery_transaction.mjs tests/test_jellyfin_recovery_manager.cjs tests/test_jellyfin_recovery_transition.cjs
```

The harness verifies source bytes, applies the patch to a unique disposable
checkout-shaped directory, and runs the actual patched manager with imports
injected as fakes. It supplies an explicit `w763-test-only` binding and a fake
player event-correlation boundary. It does not run a native player or establish
that a real host, document, adapter or terminal/settlement observer is trusted.
That boundary requires every recovery-origin callback to carry its correlation
envelope. Untagged callbacks remain ordinary events; they cannot independently
establish recovery ownership or settlement.
An error correlated synchronously during dispatch is reflected by `dispatchError`
in the begin outcome; acceptance still does not establish successful playback.
The manager's ordinary no-argument construction has no test binding and rejects
recovery. A provisional bridge attachment is not a test binding.

For a future separately authorized upstream review, apply
`0001-recovery-transaction.patch` to the pristine pinned checkout and add
`recoveryTransaction.js` at `src/components/playback/recoveryTransaction.js`.
Do not deploy the artifact or treat its test-only binding as a production trust
mechanism. A real host-bound capability, correlated player events and settlement
evidence require their own reviewed implementation and qualification.
