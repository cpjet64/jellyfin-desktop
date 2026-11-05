# macOS settlement snapshot identity failure — 2026-11-05

The single authorized W545 snapshot failed. Its protected terminal receipt,
`settlement-w520.status.json`, has SHA-256
`f977649a5269f2391eee173cfda4c23e61119f8ab4b5be83e9eb27cea95881f4`.
It records guest exit code 1, 3,234 bytes of stdout and no stderr. The
identification failure is `identity_mismatch`; the successful inventory
command reported exit 0, 26,229 output bytes and 38 ms elapsed. The report
does not contain an inventory result, so it establishes neither zero unknown
processes nor current quiescence. The lease remains held. No retry is
authorized by this report.

Both recorded probe-owned operations report successful requested cleanup,
leader reap and group settlement, with no secondary error or record
truncation. This is evidence of those operations' own settlement only; it is
not evidence that the historical acquisition or other processes settled.

The mismatch event concerns the probe's own identity. The private draft
captures its kernel identity and obtains the expected session using
`os.getsid(0)`. It parses the `ps` inventory's `sess` column as a decimal
integer and compares that value, PID, parent, UID, process group and parsed
start time to the recorded identity. The receipt includes the mismatch event
and earlier preparation identity, but does not retain the expected session or
the parsed row start time. It therefore does not prove which comparison
failed; the event does not distinguish a session mismatch from a start-time
mismatch. Private process rows are intentionally excluded here.

One hypothesis is that the `sess` field is not comparable to `os.getsid(0)`
as this draft assumes. Apple's published [ps manual](https://github.com/apple-oss-distributions/adv_cmds/blob/main/ps/ps.1)
describes `sess` as a session ID, while its published
[keyword table](https://github.com/apple-oss-distributions/adv_cmds/blob/main/ps/keyword.c)
maps it to `e_sess` as a kernel-pointer field and its
[printing code](https://github.com/apple-oss-distributions/adv_cmds/blob/main/ps/print.c)
formats that field in hexadecimal. These upstream sources are not proof of the
installed guest binary's version or behavior. The retained evidence does not
establish this hypothesis as the cause; a correction or further live
observation requires a separate decision and authorization.
