# macOS inventory command identity lifetime

This is a source finding, not an observation of PID reuse or a native
qualification. The W564 snapshot was consumed and failed; this report does
not authorize another snapshot or establish current runner quiescence.

The private snapshot draft `deps/w520-private/settlement_guest.py` captures the
inventory command's kernel identity and session while the gated child is live
(lines 121–137). The gate then executes `/bin/ps` in that same PID, collects
its output, and calls `proc.wait()` (lines 122, 139–163). Only after the
command returns does `nested_command` pass that output to `identify_inventory`
(lines 79–82). Consequently the command has already been reaped at the point
where its inventory row is compared.

The inventory associates rows with the probe, supervisor leader, and command
by PID, then applies its own-row comparisons (lines 363–389). Successful
inventory requires all three roles (lines 418–421); an earlier failure may
stop processing before every role is reached. The public diagnostic helper
`dev/ci/settlement_failure.py` records the existing comparison order, including
the session field (lines 23 and 102–124). After the row comparison, the private
draft explicitly excludes the command from the subsequent current kernel
identity check; that check is performed for the probe and leader only
(lines 390–399).

The probe itself remains active during its nested operation, and the
supervisor leader executes the inventory work and remains alive through the
response until the parent's cleanup (lines 71–82, 92–110 and 284–308). The
command has no equivalent live hold after its wait. A numeric PID retained
after reaping cannot establish continuous ownership of the original process.

The existing gated shell uses `exec` to replace itself with `/bin/ps`; the
resulting command does not implement a cooperative post-inventory handshake.
Receipt of pipe data does not prove the writer remains alive through a later
identity query, and retaining an unreaped zombie alone does not establish that
`getsid` can safely obtain the required session. A wrapper and separate child
would alter the role and PID relationship and require a separately reviewed
design. These are constraints on future design, not a validated repair or an
authorization to change the process boundary.
