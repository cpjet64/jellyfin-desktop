# Fixed VCTIP identity observation proposal

This source proposal does not authorize or report an identity query.
The fixed client uses the existing `d0bb016d` Windows identity implementation to open and pin only the VCTIP file named in `proposal.json`.
It does not launch that file, a compiler, an application, or a helper itself, and does not read the private compiler environment.
The controller proposes one hidden launch through the existing qualified outer helper into a fresh project-local evidence root.
Before any future allocation, reviewers must pin the proposal commit and the exact controller and proposal hashes independently.
The controller requires those identities as parameters and checks both source worktrees and pinned executable inputs before and after the proposed call.
The result would contain volume serial and file index observed through the held file handle, plus checked handle closure; no file ID is prefilled here.
The stated version is prior metadata bound to the expected bytes, not a fresh version-resource observation.
An incomplete receipt, inaccessible identity, failed close, storage error, timeout or unsettled job is a failure and must not be retried without a new allocation.
