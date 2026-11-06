# Healthy restart mpv delay-import timing

The W881 Windows diagnostic artifact was built from source
`7f5d64d619727ed5672660bc3d8bda70b58511f0`. Its
`test_healthy_restart.exe` has SHA-256
`68ba9b468551bc155be5dbf16abd27c7bcef56d3e69fd6c3dafe8cc3bd5934d6`.
This is a compiled, held artifact; it was not executed for this inspection.

`CMakeModules/WindowsLoaderSelection.cmake` adds
`/DELAYLOAD:libmpv-2.dll` to the relevant Windows link. Read-only
`dumpbin /imports` on that exact W881 executable reports `libmpv-2.dll`
under delay-load imports and lists `mpv_render_context_set_update_callback`
among them. This is static artifact evidence; it does not show whether that
slot is resolved at a particular runtime instant.

Consequently, a child-side gate before `MpvVideoItem` construction cannot
assume that this callback's import slot already contains a DLL implementation
address. A still-unresolved delay thunk may resolve to the executable, which
the proposed actual-address check must reject. Calling `GetProcAddress` on an
independently selected expected DLL would not establish the actual call-site
binding. An expected staging path and matching hash alone are also insufficient.

Two bounded options need review before wiring positive authorization:

1. Resolve the existing delay import through the established loader before
   item construction, if the supported linker/loader mechanism and original
   parent-held lease can be proven to bind the same call site. This requires
   explicit approval and must not load a second independent DLL.
2. Keep the pre-item gate fail-closed and verify the callback slot only after
   an independently safe, already-authorized call resolves that exact import.
   The construction and callback-registration ordering must then be revised so
   no positive retirement decision precedes verification.

The file lease, restricted channel and actual-call binding are not integrated
by this note. Neither option establishes the native callback barrier or live GL
context qualification. No DLL load, process launch, playback or native
retirement was performed.
