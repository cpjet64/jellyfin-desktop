# Proposed loaded mpv module identity gate

This is a source-design proposal. It neither qualifies the selected binary's
callback barrier nor enables render-context retirement. No process launch,
module load, native execution or platform qualification was performed.

The Windows fixture parent would open the exact controlled staging DLL before
`CreateProcess`, request read access while denying write and delete sharing,
and retain that file handle until the child and its owned job have settled.
It would hash the opened file and require SHA-256
`5c5c9d094859bdc60b8b27b2d832db8fa8c2e487efbd35f5aea5afe6960dad39`.
The staging directory and path must be the fixture's controlled destination,
not a child-supplied path. Failure to establish or retain the lease rejects the
attempt. Loader compatibility with these sharing flags remains to be tested.

The existing parent launcher would pass a duplicate of the held handle only
through its restricted inherited-handle list and bind that handle to the
owned child, job and launch attempt. This requires actual launcher and
protocol integration; a path, hash, handle value, nonce or JSON message from an
untrusted child is not authority. The parent retains the original lease through
settlement even if transfer or validation fails.

The child would identify the module containing the **actual callable export
used by the relevant mpv path**, using `GetModuleHandleExW` with
`GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS` and retaining the resulting module
reference for the context lifetime. It would obtain a complete module path,
open that file read-only, and compare its stable volume/file identity with the
inherited locked handle. Both the canonical controlled staging path and the
exact file digest must match. Short or truncated paths, failed reads or
identity queries, changed lease, and any fallback DLL reject qualification.
The evidence must separately record a complete successful hash read; a digest
buffer alone cannot establish that the read finished without error.

An imported-function address can be a delay-load or import thunk in the
executable. If that address resolves to the executable or another unexpected
module, the result is unqualified. Looking up an export in an independently
chosen expected DLL does not prove the actual call uses that module; the
implementation must establish that binding or reject. No additional library
load or mpv call is authorized by this proposal. Hash through an independent
handle or positional reads so inherited handles do not race on file position.

Any resulting immutable evidence would bind the parent-owned launch, child,
retained module reference, actual export, locked file identity and render
context generation. It describes the staged file and its mapping relationship;
it is not independent proof of mapped memory contents, complete callback
locking, or live GL context safety. Non-Windows, unsupported loader behavior,
missing handles, ambiguous identity and lifecycle changes remain unqualified.

The eventual headless seam may compare injected identity facts and reject
mismatches, but it must not construct native qualification. Tests must cover
missing and truncated paths, unavailable APIs, wrong module or file identity,
wrong hash, lease loss, duplicate/replayed launch binding, and default-disabled
behavior. No `TestQualified` path may enable the native fixture. The locked-file
and real loader interaction requires a later separately authorized validation.
