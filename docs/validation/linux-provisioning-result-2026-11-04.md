# Linux native prerequisite provisioning: 2026-11-04

The approved runtime and display package transactions completed separately.
The runtime transaction installed 77 new packages, with no upgrades. The
display transaction installed three new packages and upgraded the five
specified packages. The approved package identities, versions, architectures,
and archive hashes are recorded in [the transaction manifest](linux-provisioning-transaction.json).
The published Git blob with LF line endings has SHA-256
`04e2a4c2885514f2828b004127619051fdeec14cfcedec2992c3117b9ae21719`.
The exact CRLF bytes supplied to the transactions have SHA-256
`d1bff39e461582a2c26649c9658a45ba0710167eb386988216a52ef654411cad`.
Normalizing those transaction bytes to LF was verified to reproduce the Git
blob bytes.

Both transactions ended successfully. The retained post-install checks
confirmed each package delta against the manifest, the runtime after-state
against the display before-state, and the final package state and manual/auto
marks against the live state. The dpkg audit was empty. Before each install,
the transaction checked the exact simulated plan and verified downloaded
package identities, sizes, and hashes. Retained package-state backups were
readable and hash-verified; the subsequent read-only check verified their
hashes again. The inspected control metadata had no service-management
findings under the transaction's check.

The installed `libmpv2` package is version `0.37.0-1ubuntu4` for amd64. Its
installed library is x86-64 ELF, matches the installed package's checksum
metadata, and has SHA-256
`f2aa472878adee5f44419b6cce90ccecc43bf81595ac16dbf223de190cdead0a`.
The post-install checks also found Xvfb version `2:21.1.12-1ubuntu1.9` and
mesa-utils version `9.0.0-2`.

An initial runtime attempt stopped before installation: a non-script symbols
metadata file was 1,104,283 bytes, exceeding the original 1 MiB per-file
limit. The retained read-only check found no install log, unchanged package
state and marks, and an empty dpkg audit. The limit was corrected for
non-script metadata within the existing 16 MiB total bound, while retaining
the 1 MiB script bound; the later runtime and display transactions completed.

This evidence covers the package transactions and their recorded post-checks.
It does not establish Qt development dependencies, mpv development headers,
native Linux build or test success, a desktop session, or product recovery
acceptance.
