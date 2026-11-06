# Healthy retirement build checkpoint — 2026-11-06

At `7f5d64d619727ed5672660bc3d8bda70b58511f0`, a fresh Windows Debug
healthy diagnostic build completed and all ten selected headless tests
passed: five Player/event tests and five retirement lifetime, race, Qt,
owner and owner-context tests. The held `test_healthy_restart` executable
compiled and linked but was not run. Its SHA-256 is
`68ba9b468551bc155be5dbf16abd27c7bcef56d3e69fd6c3dafe8cc3bd5934d6`.
This is a build and headless protocol checkpoint, not an actual A/B recovery.

At the same source revision, the default system-mode build and 56 selected
tests passed, as did the static-mode build and 59 selected tests. Two copy
acceptance suites passed nine tests each; routing, native-order and Windows
smoke checks also passed. The retained
W881 and W882 proof records include exact commands, results, build logs and
artifact identities; both recorded matching start and end source revisions.

Independent review of the Windows Debug graph covered 59 compile entries and
59 Ninja dependency records. None used original Player or MpvQt sources or
headers; all selected external events without C2 or the test-only flag. The
four owner and lifetime sources appeared once, MOC used the copied Player
header, and the held target linked the separate healthy libraries. The review
does not establish the Linux, macOS or Release graph, or native teardown.
Its retained report SHA-256 is
`f44824b6a70d273b6ff75b6b15ce1fa155ab01472bc8c08706770474546ed620`.

Linux and macOS native graph validation and CI remain pending. No real media,
native A/B recovery or held fixture execution is claimed. Render retirement
and native callback-barrier behavior require their own native qualification.
