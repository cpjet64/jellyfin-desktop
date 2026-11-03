# Shared MpvQt compatibility

The Windows job-local shared library is built from MpvQt commit
`2ebbd38bf65d01be9c55d673ecd43c8bb9a6fc8e` plus the destructor compatibility
patch `patches/mpvqt/0002-shared-destructor.patch`. The patch declares the
destructor in the public header and defaults it in the implementation after the
private type is complete. It does not apply the bundled event-ownership patch.
The patch helper verifies and reports source and patch hashes before applying it
to an archived build copy, then verifies the patched hashes. The Windows helper
reports the installed DLL hash and builds and runs a consumer against the
installed public headers and imported target.

Shared MpvQt's libmpv dependency must be inspected separately from the desktop
executable. Its eager import does not establish the bundled static build's
non-AVX2 fallback guarantee. Native Linux and macOS must separately verify the
installed shared package and lifecycle; this Windows build does not establish
those platforms' behavior.
