/* Exact-image identity only; no archive is opened or read. */
#include <dlfcn.h>
#include <inttypes.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

static const char image[] = "/usr/lib/libarchive.2.dylib";
static const char *const symbols[] = {
    "archive_version_number", "archive_version_string", "archive_read_new",
    "archive_read_support_filter_none", "archive_read_support_format_7zip",
    "archive_read_open_fd", "archive_read_next_header", "archive_read_data",
    "archive_read_data_skip", "archive_read_has_encrypted_entries", "archive_format",
    "archive_filter_code", "archive_read_free", "archive_entry_pathname",
    "archive_entry_hardlink", "archive_entry_symlink", "archive_entry_fflags_text",
    "archive_entry_size", "archive_entry_size_is_set", "archive_entry_mode",
    "archive_entry_is_encrypted", "archive_entry_acl_types", "archive_entry_xattr_count",
    "archive_entry_sparse_count"
};

static int resolve_all(void *handle, void **version_number, void **version_string)
{
    const void *base = NULL;
    size_t i;
    for (i = 0; i < sizeof(symbols) / sizeof(symbols[0]); ++i) {
        Dl_info info;
        void *pointer;
        (void)dlerror();
        pointer = dlsym(handle, symbols[i]);
        if (dlerror() != NULL || pointer == NULL || dladdr(pointer, &info) == 0 ||
            info.dli_fname == NULL || strcmp(info.dli_fname, image) != 0 ||
            info.dli_fbase == NULL || (base != NULL && base != info.dli_fbase))
            return 0;
        base = info.dli_fbase;
        if (i == 0) *version_number = pointer;
        if (i == 1) *version_string = pointer;
    }
    const struct mach_header_64 *header = base;
    if (header == NULL || header->magic != MH_MAGIC_64 || header->cputype != CPU_TYPE_X86_64)
        return 0;
    printf("provider=%s\narchitecture=x86_64\nresolved=%zu\n", image, i);
    return 1;
}

static int print_version(void *number_pointer, void *string_pointer)
{
    int (*number)(void);
    const char *(*version)(void);
    char copy[256];
    size_t length = 0;
    _Static_assert(sizeof(number) == sizeof(number_pointer), "function pointer size");
    _Static_assert(sizeof(version) == sizeof(string_pointer), "function pointer size");
    memcpy(&number, &number_pointer, sizeof(number));
    memcpy(&version, &string_pointer, sizeof(version));
    const char *borrowed = version();
    if (borrowed == NULL) return 0;
    while (length < sizeof(copy) && borrowed[length] != '\0') {
        unsigned char byte = (unsigned char)borrowed[length];
        if (byte < 32 || byte > 126) return 0;
        copy[length++] = (char)byte;
    }
    if (length == sizeof(copy)) return 0;
    copy[length] = '\0';
    printf("archive_version_number=%d\narchive_version_string=%s\n", number(), copy);
    return 1;
}

int main(void)
{
    void *number = NULL, *version = NULL;
    void *handle;
    int success;
    if (!_dyld_shared_cache_contains_path(image)) return 1;
    puts("load_attempted=1");
    fflush(stdout);
    handle = dlopen(image, RTLD_NOW | RTLD_LOCAL | RTLD_FIRST);
    if (handle == NULL) return 1;
    puts("load_succeeded=1");
    fflush(stdout);
    success = resolve_all(handle, &number, &version) && print_version(number, version);
    printf("types=mode_t:%zu:%d,size_t:%zu:%d,ssize_t:%zu:%d,int64_t:%zu:%d,int:%zu:%d,pointer:%zu\n",
           sizeof(mode_t), (mode_t)-1 < (mode_t)0, sizeof(size_t), (size_t)-1 < (size_t)0,
           sizeof(ssize_t), (ssize_t)-1 < (ssize_t)0, sizeof(int64_t), (int64_t)-1 < (int64_t)0,
           sizeof(int), (int)-1 < (int)0, sizeof(void *));
    if (dlclose(handle) != 0) success = 0;
    else puts("close_succeeded=1");
    return success ? 0 : 1;
}
