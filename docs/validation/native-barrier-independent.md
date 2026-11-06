# Independent native callback barrier inspection

**Decision: UNQUALIFIED.** This independent, offline inspection reproduces
important local instruction paths but does not close the callback-field alias
and indirect-call graph. The default native retirement barrier remains disabled.
No DLL code, playback, GUI, or native fixture was executed.

The selected `libmpv-2.dll` SHA-256 was
`5c5c9d094859bdc60b8b27b2d832db8fa8c2e487efbd35f5aea5afe6960dad39`
both before and after inspection. Microsoft `dumpbin` 14.44.35229.0 supplied
the independent x64 disassembly. A separately written PE import-table parser
resolved the named IAT entries. The preferred image base is `0x180000000`;
addresses below are RVAs. `.text` starts at RVA `0x1000`, raw offset `0x400`,
raw size `0x5f8f800`; executable `.rodata` also exists at RVA `0x81f7000`.

## Reproduced local paths

| Site | Bytes and observation |
| --- | --- |
| `0x1060ea9` | `48 8d 70 30`: establish context alias `rsi = allocation + 0x30`. |
| `0x1060ec5` | `48 8d 8b 80 00 00 00`, followed by indirect call at `0x1060ecc`: initialize the lock at allocation `+0x80`, or context `+0x50`. The IAT entry `0x72c85c0` resolves to `InitializeSRWLock`. |
| `0x1060fdc` | `48 8d 05 0d 03 00 00` loads `0x10612f0`; `49 89 47 50` at `0x1060fe3` stores it in a separate dispatch allocation at `+0x50`, and `49 89 77 58` at `0x1060fe7` stores the context at `+0x58`. This indirect registration is not found by scanning only direct calls. |
| `0x1061542`–`0x1061549` | Setter computes context `+0x50` and calls thunk `0x5f8f1f0`, which jumps via IAT `0x72c8160` to `AcquireSRWLockExclusive`. |
| `0x106154e`, `0x1061552` | `49 89 7e 60` and `49 89 5e 68` write the supplied callback and context under that lock. |
| `0x1061556`–`0x1061573` | `test rdi,rdi; je 0x1061567` bypasses the callback call when the supplied callback is null; the path then tail-jumps to `0x5f8f2c0`, whose IAT `0x72c86f8` resolves to `ReleaseSRWLockExclusive`. The context slot receives the supplied context argument; it is cleared only if that argument is null. |
| `0x10612f9`–`0x106132e` | Update computes context `+0x50`, acquires that lock, reads callback at `0x1061305` (`48 8b 47 60`), tests it and skips dispatch when null. The non-null path reads context `+0x68` at `0x106130e`, dispatches at `0x1061312`, and subsequently tail-jumps to the same release thunk. |

The setter's non-null path also calls its newly supplied callback before
releasing the lock. The null path does not invoke the old pointer locally.
These observations support mutual exclusion for the displayed paths only;
they do not establish that no alternate path reads, copies, invokes, or writes
the callback fields. The dispatch allocation's own aliases and users are also
not closed. The number of relevant unresolved paths, unknown aliases, and
indirect targets is **not bounded**, so the required zero cannot be claimed.
No inference about other unrelated indirect calls is needed.

Clearing the native slot also does not settle already-posted Qt events or
application-owned callback object lifetime. Any future qualification must bind
the running fixture to this exact DLL and separately establish those lifetimes,
the actual render thread and original GL context, and no unauthorized callback
re-registration. Raw inspection scripts and outputs are local evidence, not
a native acceptance receipt.
