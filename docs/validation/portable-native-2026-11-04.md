# Standalone portable checks — 2026-11-04

These native, headless checks used a bounded source bundle from commit
`9f390329dce8e2cbf97ad30abde003d75ec5dbea`. The bundle's SHA-256 was
`6c41b942410501474a5afad63feb86b10392de57884b46e4765871d0d4b48efa`.
They are a checkpoint for that source, not a GitLab pipeline, an application
build, or proof for later commits.

| Platform | Outcome | Scope |
| --- | --- | --- |
| Linux x86_64 | 153 passed, 1 skipped, 0 failed | All 18 selected portable suites completed; the skip was the Windows-only PowerShell integration test. |
| macOS x86_64 | 119 passed, 5 skipped, 1 error | The first 13 suites completed; the fourteenth failed. Four later suites were not run. |

The macOS skips comprised the Windows-only PowerShell integration test and four
archive materialization or publication cases explicitly limited by their source
to Linux or Windows. The failing case was
`PlanTest.test_decode_reads_through_eof_before_publication`: on macOS its call
to `publish_stage` raised `RuntimeError: atomic no-replace publication
unavailable on this platform`. The other nine cases in that suite passed. This
is a failed portable test; it does not establish a product runtime failure.
The complete retained failing log was 2,725 bytes with SHA-256
`97ddac511c5f9a92dab722996933b02d108e0f5f34688f452c7e539ed668c03d`.

The Linux receipt's SHA-256 is
`3f323abfed5236a364922a82f43c431a1e8ea60e97bd0700f010ed2b358fb4a8`;
the macOS receipt's SHA-256 is
`4496a1824cfc2df95bca24076d256517314b3101e4994591052ef86c01f90a2d`.
The receipts and suite logs are retained privately because they contain local
workspace details. The Linux proof used Python 3.12.3; the macOS suites used
Python 3.13.15.

Each suite was supervised in an owned process group. The controller terminated
the still-unreaped supervisor after the suite result, and checked group
settlement; this is not a claim that the supervisor itself exited naturally.
The process-group mechanism does not contain a descendant that deliberately
creates a separate session. The selected test sources were inspected and did
not show such an unmocked launch. The macOS failing log was retrieved read-only
after the test process and owned group had settled.

An initial Linux client attempt stopped at its placement check before creating
an intent or submitting a guest command: its expected node was stale. A fresh
read-only placement and runner-identity check preceded the one Linux execution
reported above. This pre-submission stop was an operational routing issue, not
a failed product test.
