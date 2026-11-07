# Windows proof command launcher

This finite-command helper creates a fresh Windows job, assigns a suspended,
hidden root process before resuming it, and waits for both root exit and zero
active job processes. It does not own work launched by external services or
process brokers. It never reconstructs ownership from historical PIDs.

Build in an ignored task directory using the existing MSVC x64 environment:

```text
cl /nologo /EHsc /std:c++17 /W4 /WX /DW1214_TESTING job_launch.cpp /Fe:job_launch.exe
cl /nologo /EHsc /std:c++17 /W4 /WX job_launch.cpp /Fe:job_launch_prod.exe
python -B test_job_launch.py
```

The first executable includes two failure-injection hooks and is only for the
11-test helper suite. Use only the second executable for real commands. To test
that production executable, import `test_job_launch`, set its `LAUNCHER` to the
absolute production executable path, and run its nine tests excluding
`test_assignment_failure_does_not_resume` and `test_early_close_never_passes`.

Invocation:

```text
job_launch_prod.exe NEW_RECEIPT TIMEOUT_MS WORKING_DIRECTORY ABSOLUTE_PROGRAM [ARGUMENTS...]
```

The receipt is created exclusively, cannot be inherited, and remains locked
against sharing until the helper closes it. Standard output/error belong to the
child command and are not authoritative receipt channels. Success requires the
helper's observed exit code zero and exactly one of each required receipt:
`root_exit=0`, `timeout=0 active=0`, `handles_complete=1`, `final_result=0`, and
`proof_complete=1`. Reject malformed or duplicate fields. Receipt text alone
cannot prove that its final flush/close succeeded.

Native nonzero exits are recorded verbatim; helper exit codes above 123 are
reserved (124 timeout, 125 proof failure), with larger native codes mapped to
123. An unresolved settlement stops dependent operations; never fall back to
PID-based cleanup or infer success from root exit alone.

The focused tests cover immediate/nonzero exit, natural and timed-out descendant
trees, stale root information, unrelated-process survival, argument preservation,
existing receipt rejection, assignment-before-resume failure, and early job
closure. The stale-root test does not force PID reuse.

At the initial qualification, all six explicitly owned handles closed, but the
whole-process handle count increased by five for an unexplained reason. This is
not evidence of globally leak-free operation. The helper itself must exit and
be waited on by its caller. Test-mode and production-mode results are separate
from application build, playback, and cross-platform acceptance evidence.
