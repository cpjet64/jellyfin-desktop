# CL service isolation: finite qualification proposal

**HOLD: source proposal only.** No client/server association witness has been
shown to work for this CL build. Neither two environment values nor two PIDs,
job membership, file hashes, PDB timestamps, nor a successful compile or link
establishes routing. The current workstation has other agents and is not an
exclusive compiler context. Do not enable the production v2 profile based on
this proposal.

## Admission before any separately authorized execution

Select one existing Windows worker only after fresh evidence establishes an
exclusive reservation covering the *whole machine* and its compiler-service
namespace for the complete run, with no unrelated compiler, linker, or service
clients. A quiet private job is insufficient. Do not stop other work to acquire
it. Bind its machine, account, isolation lease, expiry, toolset paths, actual
tool identities, workspace containment, resource headroom and one fresh run ID
to a reviewed execution packet. Verify the exact 17-name captured snapshot and
the 18th `_MSPDBSRV_ENDPOINT_` in each child environment; use two independently
fresh `jd-<32 lowercase hex>` names A and B. Bind the exact environment bytes,
including each endpoint, in per-invocation digests and enrollment records.
Never print their values. Reject inherited or reused endpoint values.

Retain pinned CL and the exact VCTIP and MSPDBSRV files before launch. The
candidate pins are CL 14.44.35207 SHA-256
`fe251ef50a1545b1b0835ee17b1e785459712b38d79e45b5c1d3d28970a36619`
and MSPDBSRV SHA-256
`8ffbacfdbef90a79e8441d640bc8b4c86be02a2d9035ce87c5fa4f36dc3c70e1`;
the observed service size is 186248 and file identity is
`2555941978:458752:1477178`. A worker must match exact retained file identity
and bytes, not just a version label. Reconfirm the VCTIP pin from the v2 profile.
The frozen CL command and response have been consumed; make and review new,
bounded response files and enrollment identities for this packet.

## Finite experiment and required evidence

Use the existing private-job launcher and versioned build-only lifecycle, not
the application/test helper. Keep its root deadline, grace, one cleanup budget,
complete job enumeration, all-members-verified-before-intervention rule and
retained-handle termination. Do not alter the application natural-settlement
contract. The ordinary v2 success path settles its job and does not itself
provide the required two-live-server experiment. A separate, reviewed finite
qualification coordinator must retain independent A and B containment jobs,
keep each root and all service handles accounted for across the experiment,
and defer A's targeted settlement until both A clients finish and B is alive.
It must not relax the production profile's one-service limit or let A cleanup
operate on B's job. Until that coordinator, its cleanup on every exit, and its
strict receipts have been implemented and reviewed, this plan is not runnable.
These are *command forms*, not authorization or executable scripts:

1. Compile `client_a.c` twice in succession with the same endpoint A and a
   common A PDB, distinct object paths, and `/nologo /c /Zi /FS /Od`; set
   explicit `/Fd<absolute-A-pdb>` and `/Fo<absolute-A-object>`. Each invocation
   has its own reviewed response file and environment digest. Record both
   successful exits and the retained identity of the server serving each.
2. Compile `client_b.c` with distinct endpoint B and B PDB under the same
   flags; retain B's server and independently established client association.
   Require A and B to be distinct simultaneously alive service identities.
3. After **all** A clients have completed and A membership is re-enumerated,
   clean up only the verified retained A server under the existing single
   budget. Record membership and creation identity immediately before action,
   termination result and signalled handle. Any late or unknown member fails.
4. While the original B server remains alive, run a second B client writing a
   distinct object against B's same PDB. Require the independently observed
   B association to be that *same retained identity*, normal client exit, PDB
   update and server liveness across A cleanup. A newly spawned replacement is
   NONPASS, not evidence that B survived. Then settle B under its own approved
   lifecycle; do not leave task-owned clients or servers running.
5. Compile `consumer.c` with the reviewed CL and link it with one A and the
   post-cleanup B object using an independently pinned, reviewed MSVC linker,
   `/DEBUG:FULL /WX` and an explicit output PDB. The link must read the compiler
   PDBs and exit zero; inspect linker diagnostics for missing/corrupt debug
   information, not merely output-file existence or hash. Pin and review exact
   commands, input/output paths and consumer checks before any execution.

Use fresh workspace-local source copies whose hashes match this packet, distinct
PDBs for A and B, and a fresh empty output directory. Preserve command, exit,
process creation identity, handle, client/server association, PDB and link
evidence without logging endpoint or environment values. Evidence must include
timestamps and causal ordering, bounded observation loss checks, and an outcome
for each original retained process. Failure or ambiguity is terminal NONPASS;
do not retry or substitute a different CL in the same allocation.

## Unresolved association witness

Before this experiment can be approved, propose and independently review a
bounded supported observation that associates **each actual CL client** to the
specific retained MSPDBSRV process, including the second B client after cleanup.
One candidate for a separate feasibility review is a worker-scoped kernel file
and named-pipe event capture with loss accounting, correlated client and server
process creation identities, exact pipe connection and open/close intervals,
and server PDB writes. No claim is made here that these events expose all needed
connections or that this candidate is sufficient. A missing or ambiguous event,
unsupported provider, incomplete connection identity, or capture loss is
NONPASS. A PDB write or process ancestry alone cannot establish the pairing.
If a supported source cannot establish the connection, stop at this HOLD and
request one bounded observation feasibility proposal; do not add a general RPC
monitoring subsystem or infer routing from endpoint inheritance.

No endpoint probe, compiler, linker, helper, tests, CI, host mutation, or
service intervention is authorized by this source packet.
