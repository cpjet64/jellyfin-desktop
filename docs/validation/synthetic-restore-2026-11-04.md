# Windows synthetic restoration diagnostic — 2026-11-04

One Debug and one Release attempt used diagnostic binaries built from [commit `b015287b557b69a05309138c8673048b79e352e1`](https://gitlab.curtpme.com/cpjet64/jellyfin-desktop/-/commit/b015287b557b69a05309138c8673048b79e352e1), with orchestration at `db33261ecd366b56b8970f8e20355583b9b932f9`. The checked diagnostic build inputs were unchanged between those revisions. These are contained synthetic tests, not production recovery acceptance.

| Attempt | Fixture SHA-256 | Result |
| --- | --- | --- |
| Debug | `d2e2705ad3e80939a3d0cdbf07b68c671dbd0c0d251e0531dcbd1d67f19f116d` | Passed: complete QTest 3/0/0 footer. |
| Release | `8de41e4d470eec43272627bb43745bb29b500455c49a5dc9eabda1b9bcb6212e` | Failed: quiescence returned false; G1 retirement was not established. |

Both used hidden-desktop helper SHA-256 `706e0a939b4dff4b2f2996996863943c3d3a5dfba6dc46e96578f4fb73bd7a42`. Debug application and helper exited naturally with code 0. Release application exited naturally with code 1 and the helper returned 7. Both reported known zero active job processes before and after cleanup, no termination request and no forced cleanup.

Debug captured playing position 1.9 seconds, observed a pending G1 service operation and successful skip on the original generation, render thread and context, then observed Stop, canceled, idle and no VO before service closure and G1 retirement. G2 creation succeeded on that same worker and context. A fresh paused scene capture matched the captured 1.9-second position and frame counter. This capture alone does not establish that the pixels were newly decoded G2 content rather than a retained texture. Subsequent distinct images and position advancement from 1.900 to 2.133 seconds established the playing endpoint. Both retirement acknowledgements and ordered observer, core and Qt cleanup were recorded.

Release captured a playing position of 1.9 seconds with two distinct G1 images. Its first reported test failure was `control.quiesce(...)` returning false at `c2_fault_restore_case.h:130`. That operation combines several checks; the retained output does not identify the failed subcondition. It produced no complete QTest footer, G1 service or retirement record, or G2 evidence. No unchanged retry was performed.

The runner independently checked structured timing, identities and ordering, PNG signatures and motion-file inequality for Debug. The exact fixture source gates the typed synthetic failure and pixel/frame-counter interpretation. Raw local evidence is retained separately; the hashes below identify the bounded receipts and logs without publishing device or environment details.

| Artifact | SHA-256 |
| --- | --- |
| Debug receipt | `1cb8586015bc21e20d37df0c39f2173fb6fa3905f60b2acb6e2bf38c85a3c919` |
| Debug QTest log | `08bf92ad120795246411edfbac56d3b6a2be03ae0608686c6a11a931db50aad8` |
| Debug helper log | `90eb0a4ee8e111387152a18471d7c1feb62e0fe5b364b4ad1a9d42abc9a3055a` |
| Release receipt | `bf3aa71831a5e5a025197e283b372791bfe03aeee7736b93b38b321d6a731c0d` |
| Release QTest log | `0d043cd458ba55c7216ee680b5ae861cbe42d968284579cd46c39515b637bbe4` |
| Release helper log | `0fc6484fcb3c5f367515915af02a37e41de7b146ddf7d1e8291914b98ce3a786` |
| Release runner log | `807a9c0837819de273a497550a7276d16ff3a2a28d305bb62196b8682ee2c5a4` |
