#define _GNU_SOURCE
#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpv/client.h>
#include <mpv/render.h>

int main(int argc, char **argv)
{
    Dl_info info;
    unsigned long runtime_api;
    char loaded[PATH_MAX], expected[PATH_MAX];
    if (argc != 2 || !dladdr((void *)mpv_client_api_version, &info) ||
        !realpath(info.dli_fname, loaded) || !realpath(argv[1], expected) ||
        strcmp(loaded, expected) != 0) {
        fprintf(stderr, "Loaded libmpv identity does not match requested DSO\n");
        return 1;
    }
    runtime_api = mpv_client_api_version();
    printf("header_api=%lu runtime_api=%lu loaded=%s\n",
           (unsigned long)MPV_CLIENT_API_VERSION, runtime_api, loaded);
    if (runtime_api != (unsigned long)MPV_CLIENT_API_VERSION) {
        fprintf(stderr, "Header and runtime client API versions differ\n");
        return 1;
    }
    return 0;
}
