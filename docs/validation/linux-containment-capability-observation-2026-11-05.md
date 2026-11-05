# Linux containment capability observation — 2026-11-05

One authorized read-only inspection completed on VMID 201, inventory name
`ubuntu-runner-201`, at PVE-3. The guest-reported hostname was `runner`. The
observation describes the QEMU guest-agent process and that same process after
an inline identity drop; it is not a runner-service or PAM-session observation.
It does not qualify containment or authorize a unit, installation, fixture,
signal, or other host change.

## Evidence identity

- Source revision at submission: `4ad20ccc2178d7029240339d5b062d53b7b79133`.
  The raw collector bytes were 9,286 bytes, SHA-256
  `d1e9ff04eb10943efcc248ac874cb7d5ea090beb773fac308d295ef4ed19940c`.
- Private guest driver SHA-256:
  `70ee9a8d7470d71c4c809ee18b0405e739fc59bb657e9181375a9fc464752db9`.
  Reviewed submission adapter SHA-256:
  `f06a8a892bb99794b5e71b3cec7f21d4d1b7fdf660697c477626b4e694dc38f4`.
- Task `887d2a3e596c41d1801249a6cd551c81`, guest-agent PID `3786068`.
  The retained URL-encoded request was 16,095 bytes, SHA-256
  `7715f40dd551e7cc43aa07aab46ae154ed7152a32e918ec948e11c6bb7d197f0`.
- Raw terminal response SHA-256:
  `2536c303af56a62ee1457648ce0e907f7c1826b0802f087ed9f5956fc77fc4ab`.
  It reported exit 0, 6,200 stdout bytes, no stderr, and no truncation flags.
  Terminal observation time was `2026-11-05T09:51:30.363975Z`. The protected
  task journal retains the request, intent, PID and raw terminal response.

The earlier identity-preflight failure and its protected journal are separate
evidence. The earlier `eb05a8e3c145449aa863b2c528e10bb0` local preparation
directory remains preserved. The coordinator reported that its diagnostic helper
returned a `SeSecurityPrivilege` failure while the ACL identity comparison was unchanged.
That helper failure is not a successful preparation or a guest observation.

## Observed context and access

| Observation | QGA controller | Same process after inline drop |
| --- | --- | --- |
| Account and IDs | `root`, UID/EUID 0, GID/EGID 0 | `gitlab-runner`, UID/EUID 999, GID/EGID 988 |
| PID and membership | 3786068; `/system.slice/qemu-guest-agent.service` | Same PID and membership |
| Observed cgroup directories | Write access reported at QGA service, `system.slice`, and cgroup root | No write access reported at those three paths |
| `cgroup.kill` | Read and write access reported at QGA service and `system.slice`; absent at root | No read or write access reported at QGA service and `system.slice`; absent at root |
| System manager socket | Root-owned mode `0700`, read/write access reported | Read/write access not reported |
| User manager | User socket and bus absent; user-manager query failed | User socket and bus absent; user-manager query failed |

Both contexts reported systemd `255 (255.4-1ubuntu8.18)`. The fixed
`gitlab-runner.service` reference query reported `Delegate=no` and
`ControlGroup=/system.slice/gitlab-runner.service`; it is a reference unit,
not the observed process membership. The reported access checks reflect these
specific credentials and paths at the observation time. They do not establish
manager authorization, delegation, or a durable capability.

The controller's reported write access does not authorize killing or changing
the shared `qemu-guest-agent.service` cgroup. No independently owned containment
boundary or safe cleanup path was established, and no native containment
qualification, transient unit, package operation, or installation was run.
