# Linux capability preflight observation — 2026-11-05

One separately authorized conditional preflight completed with terminal exit 0. Its protected receipt, retained at `deps/w551-containment/877d7a4ccf3d41849837c77a773b3c9b/terminal-status.json`, has SHA-256 `3bfe80c77b9d6dd9e3af718c6d65396567c395befbcfe5ed91c276d2b8aaa4e7`; it records 249 standard-output bytes and empty standard error. This is a successful **observation**, not a capability pass.

The coordinator's bounded review recorded `collector_match=true`: the decoded collector was 9,286 bytes with SHA-256 `d1e9ff04eb10943efcc248ac874cb7d5ea090beb773fac308d295ef4ed19940c`. The expected hostname was `ubuntu-runner-201`, while the observed hostname was `runner`, so `hostname_match=false`; the observation's error field was null. The expected hostname was not changed. No collector import or execution, UID drop, capability inspection, package installation or retry occurred.

The exact retained request was 15,117 bytes, SHA-256 `47f4dcb48f548cd8a94485db0d7e12ac2c2e4f10326d4b8c9f2531f33ce1d3ae`; the source revision was `e09bf727b5c42ba7b4156929e68703121dfb8117`. Fresh API checks validated VM 201 on PVE 3 and recorded runner registrations 3, 13 and 19 online and idle with zero running jobs. The existing lease remained in force; the receipt's access was restricted to the user, SYSTEM and Administrators.

The [earlier W551 failure](linux-capability-preflight-failure-2026-11-05.md) remains an ambiguous combined-guard result. This later observation separates the two predicates for this attempt; it does not retroactively prove which predicate caused the earlier failure or authorize a capability run.
