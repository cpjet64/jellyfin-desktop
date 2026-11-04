# macOS exact reader identity probe

Status: source proposal; no runtime image has been loaded by this probe. The
probe resolves only `/usr/lib/libarchive.2.dylib` with the selected SDK's
`RTLD_NOW | RTLD_LOCAL | RTLD_FIRST`. It checks `dlerror` and `dladdr` for
every pointer, requires the exact provider path and one image base, checks only
fixed Mach-O header fields, calls only the two version functions and closes the
handle once. A path or provider mismatch fails closed. No archive operation is
invoked. The returned version string is copied with a fixed bound while live.

The runtime version must be reconciled against the declaration provenance below
before any reader call. The probe prints selected SDK size and signedness facts;
it does not establish the library's ABI or 7z capability. Mach-O 9.2 is an
image version, not a libarchive software version.

## Signature table for review

This table transcribes Apple's 3.7.4 declarations in its pinned
[archive.h](https://github.com/apple-oss-distributions/libarchive/blob/5649597e7975dd1f8c24ab0176f131f47a1cdae0/libarchive/libarchive/archive.h)
and [archive_entry.h](https://github.com/apple-oss-distributions/libarchive/blob/5649597e7975dd1f8c24ab0176f131f47a1cdae0/libarchive/libarchive/archive_entry.h).
`A` means `struct archive *`, `E` means `struct archive_entry *`. This is
declaration provenance, not evidence of the loaded implementation's version.

| Symbol | C declaration shape |
|---|---|
| `archive_version_number` | `int (void)` |
| `archive_version_string` | `const char * (void)` |
| `archive_read_new` | `A (void)` |
| `archive_read_support_filter_none`, `archive_read_support_format_7zip`, `archive_read_data_skip`, `archive_read_has_encrypted_entries`, `archive_format`, `archive_read_free` | `int (A)` |
| `archive_read_open_fd` | `int (A, int, size_t)` |
| `archive_read_next_header` | `int (A, E *)` |
| `archive_read_data` | `la_ssize_t (A, void *, size_t)` |
| `archive_filter_code` | `int (A, int)` |
| `archive_entry_pathname`, `archive_entry_hardlink`, `archive_entry_symlink`, `archive_entry_fflags_text` | `const char * (E)` |
| `archive_entry_size` | `la_int64_t (E)` |
| `archive_entry_size_is_set`, `archive_entry_is_encrypted`, `archive_entry_acl_types`, `archive_entry_xattr_count`, `archive_entry_sparse_count` | `int (E)` |
| `archive_entry_mode` | `mode_t (E)` |

`mode_t` must not be assumed to match Linux's `unsigned int` binding. The
selected SDK facts for `mode_t`, `size_t`, `ssize_t`, `int64_t`, `int` and
pointers must be captured and reviewed along with the runtime version and
Apple's `la_ssize_t` / `la_int64_t` definitions. Name presence does not prove
function signatures, return semantics or binary compatibility.

## Execution boundary

Before a separately reviewed one-shot run, confirm the exact OS build,
architecture, SDK, stub and compiler identity and active-cache membership;
clear loader override variables and preserve a bounded receipt. The probe must
be compiled for x86_64 against the selected SDK, checked to have no libarchive
link dependency, and run under the reviewed finite native supervisor only after
the exclusive Mac lease is released. Any mismatch, missing symbol, unresolved
provider, version-copy error or close failure stops without fallback. Runtime
review precedes fixture execution. No archive acquisition, extraction, SDK
publication or reader readiness is implied.
