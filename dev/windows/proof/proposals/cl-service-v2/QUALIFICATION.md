# Compiler service profile: qualification proposal

This is source-only. Production admission remains disabled. The prior discovery
attempt is terminal NONPASS, and the existing fixture qualification covers only
the old one-role contract. No compiler, endpoint probe, test, or worker setup is
authorized by this proposal.

The candidate profile binds the reviewed `/Zi /FS` response, VCTIP and the
observed MSPDBSRV image, a fresh invocation-specific endpoint, the original
seventeen environment names plus `_MSPDBSRV_ENDPOINT_`, and the resulting digest.
The displayed original output root was consumed by the failed attempt. A fresh
output and its revised exact response/policy must be independently reviewed before
any new compile; this draft neither reuses the old directory nor claims a new
response identity. The v2 package digest also awaits the assembled candidate.

The finite qualification must use an exclusive Windows build worker or an
equivalently demonstrable context with no unrelated compiler clients. No worker
provisioning or interruption of another build is authorized here. A reviewed,
bounded association observation method for this exact CL and server is required
before commands can be allocated; process count, image hash, job membership and
environment strings alone do not establish which endpoint served a client.

After that method is reviewed, the packet must prescribe one bounded sequence:

1. Run controlled CL clients with one fresh endpoint and observe the specific
   retained server identity that serves those clients.
2. Run a controlled client with a distinct fresh endpoint and establish a
   different retained server identity and its client association.
3. Once all first-endpoint clients have finished, target only its verified
   service handle; demonstrate that the second server and client still work.
4. Prove complete settlement and close accounting for both controlled jobs.
   Any unknown process, ambiguous association, timeout, unclosed handle, or
   unverified isolation makes the qualification NONPASS.

Each qualification invocation needs an exclusive fresh task-owned output root,
fixed reviewed response, 60-second compiler root deadline, at most one second
grace, one ten-second cleanup budget and an independently settled outer job.
Per-case exact command, observed association mechanism, binary pins, endpoint
allocation, logs and receipt identities must be fixed in the subsequent review
packet. Existing lifecycle and receipt workers supply focused source tests for
mixed roles, ambiguity, unknown members, late association, failure, deadlines
and root failure. No PDB usability claim follows from an output hash: a later
controlled compiler/link consumer check is required before accepting outputs.
