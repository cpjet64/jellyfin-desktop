# macOS archive reader identity: metadata observation

On 2026-11-04 a bounded metadata probe completed on the designated Intel macOS
runner. The private receipt's SHA-256 is
`3871ecda3f0b61b70628afcb93e88bae3085d34b9402056099889db198c60d64`.
This report omits the host and account identifiers from that receipt.

The observed OS was macOS 26.6.2, build 25G83, architecture `x86_64`;
the selected SDK was 26.5, resolved to
`/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk`.
`otool -L /usr/bin/tar` exited 0 and recorded a dependency install name
`/usr/lib/libarchive.2.dylib`, compatibility version `9.0.0`, current version
`9.2.0`. These are the executable's recorded link metadata, not a runtime
reader version or a claim that the library is independently usable.

The SDK's `usr/lib` immediate entries included `libarchive.2.tbd` and
`libarchive.tbd`; its `usr/include` had no names beginning `archive`. The
probe did not inspect the two stub contents. `xcrun --find
dyld_shared_cache_util` exited 72 and reported the tool unavailable. `sw_vers`,
`uname`, developer and SDK selection, `otool`, `codesign -dv`, and `pkgutil
--file-info` each exited 0. The signature command displayed signing metadata;
it was not a signature verification. Every recorded command group settled and
the task workspace was removed; this does not prove that a system utility
could not have independently daemonized a process.

No reader image was loaded, no archive was read, and no cache was extracted.
The runtime image identity, exported ABI, reader behavior and 7z support remain
unproven. Apple explains that system images may reside in the shared cache,
and that cache implementation details can change: [Apple DTS](https://developer.apple.com/forums/thread/842864).

## Proposed controlled follow-up

This is a proposal for separate review, not authority to run it. Keep the
existing native lease and bounded-process controls, preserve partial receipts,
and stop on identity mismatch. Use no search fallback, cache extraction,
custom cache parser, install, or unrelated library load.

1. Reconfirm the OS build, architecture, selected SDK, and exact tar link
   metadata. Inspect only the two exact SDK stubs named above, using identified
   existing Apple tooling after its supported invocation is reviewed. Record
   their identities, install names, architectures, version fields and the
   presence or absence of the specific structured-reader symbols required by
   the proposed adapter. Stub exports describe the SDK interface; they do not
   alone establish what the active OS image exports or how it behaves.
2. In a bounded Intel probe using the known Apple system runtime, call Apple's
   public `_dyld_shared_cache_contains_path` for the single literal
   `/usr/lib/libarchive.2.dylib`. Apple's [dyld header](https://github.com/apple-oss-distributions/dyld/blob/main/include/mach-o/dyld.h)
   describes this as a cache-presence check. A positive result establishes
   presence for that path in the active cache, not image provenance, ABI,
   version or 7z support; a negative result ends this path without fallback.
3. Before any explicit `dlopen`, review whether the combined OS build, exact
   install name, cache-presence result, selected SDK declarations, architecture
   and OS provenance identify an acceptable Apple image. If that evidence is
   insufficient, stop. Only after explicit review of that image and the probe,
   resolve that exact install name without fallback and record the loaded image
   identity and runtime version through available Apple dyld APIs. `dlopen`
   can run image initializers; the earlier metadata and presence checks do not
   authorize or substitute for that review. Apple's header documents
   `NSVersionOfRunTimeLibrary` for an already loaded library; a missing result
   or identity mismatch is a stop, not a reason to search elsewhere.
4. Treat cache presence and version as identity evidence only. A separate
   reviewed structured-reader fixture must establish callable ABI, bounded
   error behavior and representative 7z methods and metadata before considering
   actual Qt archives. If the OS image cannot be qualified, evaluate a pinned
   job-local reader build separately; do not silently substitute one.
