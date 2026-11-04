# macOS helper acquisition attempt: TLS failure

One bounded acquisition was attempted from published source commit
`778b6eb3a00ae3ca0c42d499bae6299ad78512bd` under the coordinator's exclusive
macOS runner lease. The source Git bundle arrived and was verified at 6,917,722 bytes,
SHA-256 `a4a98b7bcf5bcdc848abd0e8664e03a4eda9b71bdd46cdaa752a4154934be5fc`.
The 106 source chunks and final digest settled successfully. This source
transfer was separate from the approved helper acquisition budget.

The one acquisition process exited with code 1. Its original
terminal status recorded stderr presence but did not retain stderr. A single
subsequent GET-only query for that PID returned HTTP 500; it was not retried.
The privately retained, bounded metadata probe receipt has SHA-256
`a1df55e5421f62d554a580b75787654f14819bad6c36317def1751e418f52da9`.
The probe's own process reached terminal status. It performed no helper
execution and does not establish settlement of the earlier acquisition group.

The probe found the task-owned source bundle, checkout and a 5,365-byte
`acquire.log`. The Python 3.13 executable was a regular root/admin-owned file
with mode 0775; its canonical path matched the selected path. The worker
therefore started: this was not a pre-fork Python alias failure. In the log,
the first asset's HTTPS opener raised `SSLCertVerificationError`, reporting
`CERTIFICATE_VERIFY_FAILED` and inability to get a local issuer certificate.
No conclusion is drawn about why the selected Python trust configuration could
not validate that chain, or whether any bytes reached network buffers.

The same traceback then shows `AttributeError` in `storage_exhausted` at
`dev/ci/mac_helpers.py:51`: while traversing a wrapped TLS error, it followed
a string-valued `reason` and attempted to access its `__cause__`. This masked
the original error during exception classification; the `finally` block still
attempted stage cleanup. The selected READY and asset paths were
inaccessible because an intermediate component was not a traversable,
non-symlink directory in the point-in-time metadata snapshot. **No helper
asset has been verified as acquired.** That observation
does not prove the absence of other files or settle the earlier process group.
Private raw probe and failure evidence are retained; no retry, cleanup, trust
change, helper launch or static load was performed.

## Narrow trust proposal, not authorization to retry

First independently identify the actual certificate chain and the selected
Python interpreter's trust behavior using reviewed, read-only, bounded
diagnostics. Establish whether any proxy or interception is involved; do not
infer that the server or local trust store is defective from this error alone.
If a missing trusted issuer is confirmed, a candidate is a task-owned PEM
containing only an independently authenticated required public trust anchor,
with recorded origin, byte count and SHA-256, used through an explicit Python
SSL context that retains certificate-chain and hostname verification. The
anchor must be demonstrably appropriate for the observed chain; an arbitrary
downloaded or merely present certificate does not qualify. Alternatively,
evaluate an already installed native client only after proving its actual
system-trust semantics and preserving the reviewed redirect, byte and storage
limits. No global certificate store, interpreter installation, PATH or system
configuration change is proposed. Disabling TLS verification is excluded.

Before any subsequent acquisition proposal, repair and test the exception
classification and evidence handling, establish the earlier task's owned
process settlement, account for retained task storage and obtain a decision
on whether another attempt is authorized. An acquisition result, signature,
Mach-O inspection, self-extraction bound, native execution and Desktop
acceptance all remain unproven.
