# Linux Qt archive reader proposal

The pinned CMake 4.1.2 archive interface cannot provide the structured member
metadata needed by the approved pre-write validation contract. Its verbose
listing prints raw names and link targets, which can be ambiguous, and its
extraction path does not expose a caller validation hook before writing.
See the [pinned implementation](https://raw.githubusercontent.com/Kitware/CMake/v4.1.2/Source/cmSystemTools.cxx).

A read-only check on the designated Linux runner found an existing structured
reader, without installing or updating anything:

- Package: `libarchive13t64:amd64`, installed version `3.7.2-2ubuntu0.9`.
- Library: `/usr/lib/x86_64-linux-gnu/libarchive.so.13.7.2`, 854,168 bytes.
- SHA-256: `2d8605282b26359a0994017373bff09fc7c9a6ed9ab7e1a4fd980b8ffcf1cd40`.

Proposed implementation: a narrow Python binding to this existing library's
structured read API, with explicit argument/return types, checked status and
bounded data reads. Validate package ownership, version, library identity and
ELF architecture before loading it. Enumerate and validate members before
manually writing a fresh job-owned staging tree; retain the approved path,
link, overlay, mode, size and publication checks. No archive write API, shell
extraction, new host package, or silent fallback would be used.

This is a proposal, not an implemented or tested extractor. Package integrity
metadata, archive-format support, actual Qt archive layout, symlinks,
permissions and native extraction remain to be verified. The generic tar/ZIP
extractor and default/macOS routes would remain unchanged.
