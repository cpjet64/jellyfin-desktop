# Proposed pure source gate

This is a source proposal only. It does not authorize execution. It selects exactly the profile, receipt and lifecycle source suites from the frozen `05a32b50c1a393d029b0d16485f47976b200020c` tree, using the retained v1 proof inputs from `d0bb016df169b663567e88fd5586ab7c008e9554`. It does not run the old fixture suites, compiler, endpoint or application. The Windows native helper only contains the one fixed Python child; the test suites themselves use fake observations. These results would not establish native behavior, endpoint isolation, PDB usability or product acceptance.

The proposed controller validates clean worktrees, raw Git blobs, source hashes and the 21-file v1 manifest before creating the fresh ignored output root `compiler-contract-v2-review-20261114/deps/pure-test-v2-20261114-02`. It retains input files using the existing `RetainedInputs.cs` boundary before launching. It runs the pinned Python using `-I -S -B -X` with an empty private cache, captures both streams, requires the helper and child exit zero, a strict nine-line measured outer receipt, a nonzero actual test count and no failures, errors or skips. It checks retained inputs and source again, closes pins and restores process-local TEMP and TMP. The trusted installed Python standard library is an explicit environmental trust boundary; this packet does not attest it.

After independent review and a separate execution allocation, use the exact committed proposal SHA and reviewed SHA-256 of `pure-controller.ps1` as the two values below. Launch the PowerShell process itself without a visible window and wait for it; this command is not an authorization to execute.

```powershell
pwsh.exe -NoLogo -NoProfile -NonInteractive -File E:/CursorAI/JellyfinDesktop/deps/worktrees/compiler-pure-tests-v2-20261114/dev/windows/proof/proposals/cl-service-v2/pure-tests/pure-controller.ps1 -ReviewedProposalSHA <reviewed-proposal-commit> -ReviewedControllerSHA256 <reviewed-controller-sha256>
```

The helper receives a 60-second budget; the parent waits at most 65 seconds and reserves a total 70 seconds for helper settlement and pipe drains. Timeout, close failure, stale inputs, output-root reuse or incomplete receipt is nonpass. Preserve the evidence on failure. No general command or target selector is exposed.
