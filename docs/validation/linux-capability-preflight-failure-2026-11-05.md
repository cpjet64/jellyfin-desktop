# Linux capability preflight failure — 2026-11-05

One conditional W551 inspection was consumed and failed before capability metadata was collected. The protected terminal receipt, retained at `deps/w551-containment/612e1f66e90d46c4be1f03b20cdc01e0/terminal-status.json`, has SHA-256 `67b6451f656369029f95f976ff1400258b66cc8bdacc364460f0598ef59b4bb6`. It records terminal exit 1, zero standard-output bytes and 151 standard-error bytes. No retry was performed.

The coordinator's bounded receipt review identified a `ValueError` at driver line 41 for “collector or guest mismatch”. The guard combines the encoded collector SHA and the guest hostname, so this result does not identify which comparison failed. The failure occurred before collector invocation and UID drop. No containment metadata, capability qualification or collector cleanup outcome can be inferred from it.

Before the attempt, a fresh cluster GET reported VM 201 as `ubuntu-runner-201` on PVE 3 and running. That is the hypervisor's name for the VM, not proof of the guest hostname. Fresh runner API observations recorded registrations 3, 13 and 19 online and idle with zero running jobs. Neither observation resolves the failed guard or qualifies containment. The protected receipt's access was restricted to the user, SYSTEM and Administrators. No guest repair or further guest action is implied by this report.
