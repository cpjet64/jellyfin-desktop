# macOS reader cache presence: bounded evidence

The reviewed one-time probe completed on 2026-11-04 on macOS 26.6.2 build
25G83, `x86_64`, with SDK 26.5. Its private receipt SHA-256 is
`8f16abd6f38fc746c0e5b3fb26f134f94d8f96e8cd4a6a14da8707d0a0ccd56b`.
All recorded command groups settled and the task workspace was removed. This
does not prove that a system utility could not independently daemonize.

The selected SDK's `usr/lib/libarchive.tbd` resolved within the SDK to
`usr/lib/libarchive.2.tbd`. Both preserved contents are 17,829 bytes, SHA-256
`57b7fc6ebf3c7bd1b0c4e2ec4135771c5e4e95b7b9c8888b228d5fc5d59e50cf`.
Inspection of the preserved stub shows `x86_64-macos` in the applicable export
group, install name `/usr/lib/libarchive.2.dylib`, current Mach-O version 9.2
and compatibility version 9. All 23 exact symbol names in the current Linux
reader's declared ABI occur in that export group, including the 7z registration
function. This is SDK stub evidence, not proof of runtime exports, calling
conventions, software release or 7z behavior. Mach-O version 9.2 is not a
libarchive software version.

One x86_64 C probe compiled against the selected SDK's actual
`<mach-o/dyld.h>` and returned success from
`_dyld_shared_cache_contains_path("/usr/lib/libarchive.2.dylib")`. Before
execution, `otool -L` showed only `/usr/lib/libSystem.B.dylib` as a dynamic
dependency, not libarchive. Probe source SHA-256 was
`210322ee407884092da9efb9c6c252cd34a7af94fad16e409bb9ec09072a44c7`;
binary SHA-256 was
`144341f7729939589d567265099b3f0b8f6e0b1728c159ad692c49a98f82190c`.
Apple's [public dyld declaration](https://github.com/apple-oss-distributions/dyld/blob/fd8d0c4d52320ebf64db34f3cb280310d905c5ae/include/mach-o/dyld.h)
describes this as a check for a path in the active cache. No reader image was
explicitly loaded and no archive was read.

## Proposed next qualification, for separate review

1. Reconfirm the exact OS build, Intel architecture, SDK stub identities and
   active-cache membership. Review whether those facts and Apple OS provenance
   suffice to approve loading **only** `/usr/lib/libarchive.2.dylib`; do not
   search or fall back to another image. `dlopen` can execute initializers, so
   the presence probe does not authorize it. If identity changes, stop.
2. If that exact load is separately approved, run a bounded, reviewed probe in
   the existing native lease and containment. Record the resolved image path,
   architecture and runtime image version using Apple APIs, plus the
   libarchive software version from the identified image. Resolve and check
   every required function with explicit, independently checked C signatures;
   missing symbols, unexpected identity or ABI uncertainty stop the attempt.
   Do not infer ABI safety from a stub name or Mach-O version.
3. Separately qualify reader behavior using small, known-hash seekable 7z
   fixtures. Exercise representative compression methods, regular files,
   framework-style symlinks and relevant metadata, bounded streaming, replay,
   truncated and malformed input, and explicit errors. Compare exact metadata
   and payload digests against independent expected values. A successful format
   registration alone is insufficient. Actual pinned Qt archives require
   later, distinct method and layout evidence before acquisition or SDK-ready
   claims.

No runtime loading, fixture execution, archive acquisition or native adapter is
authorized by this report. If the OS reader cannot meet the contract, return for
a separately scoped pinned job-local reader decision rather than silently
switching providers.
