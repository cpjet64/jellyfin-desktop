# macOS snapshot: inventory command row unavailable

The single W624 snapshot was consumed and failed; there is no retry authority
in this report. The protected raw terminal receipt has SHA-256
`dd8218da42bd49ee2a367aa423eb203418684d622b033f75beefdb31c1125a05`.
It records exit 1, 10,864 bytes of stdout and no stderr, with primary failure
`identification/identity_unavailable`. The retained request is 57,492 bytes,
SHA-256 `51239e2d8f230f824a98bf11393eea0e19a1b9ad8101c3b7eed228acfff400cb`.

The inventory command reports exit 0, 26,229 bytes collected in 38 ms, no
output bound and no truncation. The probe and supervisor leader comparison
records each show all six fields matched, including the live session check and
normalized start. The command's expected-row record has all fields marked
`not_evaluated`; the final report has no inventory result. Both recorded
probe-owned operations report successful requested cleanup, leader reap and
group settlement; neither records a secondary failure or truncated record.
These outcomes do not establish current quiescence, zero unknown processes,
or settlement of the historical W425 acquisition. The runner lease remains
held.

The submitted draft pins private guest SHA-256
`406b135da6e1bc005f70312c274917514c8f63fd54dde8c32f77bf63ad1c699a`,
public helper SHA-256
`84e76aeb72dec4d639a3bc93f1e5eb4667956b7163789ac04419a181997d7c1b`,
and identity helper SHA-256
`c88de12333a4f8be5b5c57b5290ddb79d19eba9141334220a030eca91db17c2e`.
The public helper was integrated at `98167cfad4ad3edabc468abea3731a2fa1c97736`.
The executed private client SHA-256 was
`9fdc47988fb44cb5795c5268d959483ad98e209124b8b4911158cd0ddc3b5ea3`.

Before submission, the coordinator observed runners 2 and 12 online, with
empty lists from the running-job query. That query does not establish the
absence of jobs in every other status. The same client checked the running
VM 114 placement on PVE-3. The protected receipt directory's observed owner
was Curt, and its allowed principals were Curt, SYSTEM and Administrators. These are
pre-submission observations, not a claim of current state.

The private guest invokes `/bin/ps -ww -axo` with fields `pid`, `ppid`,
`pgid`, `sess`, `uid`, `lstart` and `comm`. Its fixed gated shell executes
that command in the owned child; after bounded collection and a successful
wait, the parser filters to UID 501 and associates own rows by the captured
PID. A completed inventory requires probe, leader and command rows. The raw
inventory text is intentionally not included in the terminal report. The
missing-command record therefore shows that no command row was associated
among the accepted rows; it does not reveal whether a corresponding row was
absent from `ps` output or excluded by the UID filter. It does not prove PID
reuse or identify a platform defect.

One hypothesis is that the command did not appear in the process enumeration
or did not survive the draft's UID filter. The retained result does not
distinguish these possibilities. Apple's published [ps manual](https://github.com/apple-oss-distributions/adv_cmds/blob/main/ps/ps.1)
describes `-a` with `-x` as including processes without controlling terminals;
it does not establish the version or behavior of the guest's installed binary,
nor explain this particular missing row. The process-list data needed to
resolve the cause was not retained in this report. Any new collection or
change of the process boundary requires a separate decision and authorization.
