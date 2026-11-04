# macOS toolchain and filesystem observation

On 2026-11-04 at 10:18 UTC, a bounded native probe completed on an x86_64
macOS runner. Its private result receipt has SHA-256
`929b4e8ce6e4c25831ae702569a646f07139314e2fe751d02cc5c03a6ca519ee`.
The probe created a dedicated temporary workspace, removed it after success,
and reported that its child process groups settled. These observations apply
to that runner and workspace at that time, not to all macOS installations.

## Toolchain

- The selected SDK was macOS 26.5, and Apple clang was version 21.0.0.
  The checked C++ compiler alias resolved within the selected developer
  directory to a universal x86_64/arm64 executable. Its SHA-256 was
  `f30550eab15fdf5ab8c0dc54c52679711241e5d4b636b027e18c09fef531775d`.
- Small x86_64 C++ and Objective-C++ consumers compiled and linked, the latter
  against Foundation. The resulting binaries were not executed and no GUI was
  initialized.
- CMake and CTest 4.4.3, Ninja 1.13.2, Python 3.13.15 and Node 24.14.0 were
  available. Meson was not found. The workspace had 43,414,716,416 bytes free
  before compilation.

## Filesystem and archive boundary

- Case variants and composed/decomposed Unicode names collided on the tested
  workspace. A hardlink shared its target's inode. A symbolic link retained its
  relative target and reported mode `0755`; Linux's observed `0777` behavior
  must not be assumed on this macOS workspace.
- Darwin's exclusive rename succeeded when the destination was absent. With an
  existing destination it returned `EEXIST` and preserved both sentinel
  contents and directory inodes.
- The probed system libarchive path and selected SDK archive header were not
  present as files. No archive library was loaded; a usable structured archive
  reader and 7z capability remain unproven.

This probe did not acquire or build Qt or media dependencies, execute archive
policy tests, run the application, or test a GUI. It does not establish macOS
build or product readiness.
