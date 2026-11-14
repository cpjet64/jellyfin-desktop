# Compile-only enrollment proposal

This is a review artifact for one fresh compile of `test_healthy_cleanup_claim.cpp`.
It is not an enrolled production policy, permission to compile, a link plan, or a test result.
The source, tool, selected observed header hashes, fresh output paths, response bytes and private-environment digest are in `proposal.json`.

`cleanup-claim.rsp` is derived from the already captured Ninja command description, not a new compiler invocation.
That description has no double quotes, so its whitespace-delimited tokens were retained in order; the two project include roots, source, `/Fo`, and `/Fd` are the only substitutions.
The captured `/showIncludes` switch is retained; the earlier proposal omitted it.
The future direct `CreateProcessW` application path is the pinned long `cl.exe` path, with the one exact `@response` argument in `proposal.json`.

The compile output directory did not exist when inspected. It must be freshly created under the task-owned policy worktree after authorization and rechecked at use.
No object, generated file, library, or prior output is reused as a compile output. This proposal does not authorize a link.
The integration include tree and candidate header have the same Git blob for `graph_access.h`, but their raw checkout bytes differ; the proposed include roots use the candidate checkout.

Enrollment remains blocked on the exact missing inputs listed in `proposal.json`: VCTIP file identity, complete actual header/tool dependency closure and graph freshness, private environment replay with safe scratch paths, response parsing and output review, and reviewed production enrollment source.
The 17-name environment digest was calculated inside the ACL-checked pinned private-file client; its values were not printed or published. An altered environment needs a new digest.
The selected Qt, MSVC, Windows SDK and project header hashes are observations, not a complete dependency lock or proof of artifact compatibility.
Until the blockers are resolved and Pro reviews the concrete enrollment, the production registry stays empty and no compiler, test, application or CI execution is authorized by this proposal.
