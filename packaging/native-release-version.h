/* Validate that the selected release tag and installer basename identify the same build. */
#ifndef INFILTRATOR_NATIVE_RELEASE_VERSION_H
#define INFILTRATOR_NATIVE_RELEASE_VERSION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
static bool native_release_version_from_url(const char *url, const char *release_prefix,
                                             const char *asset_prefix, const char *asset_suffix,
                                             char *output, size_t size)
{
    if (!url || !release_prefix || !asset_prefix || !asset_suffix || !output || size < 2)
        return false;
    const size_t prefix = strlen(release_prefix);
    if (strncmp(url, release_prefix, prefix) != 0 || url[prefix] != 'v') return false;
    const char *start = url + prefix + 1;
    const char *slash = strchr(start, '/');
    if (!slash || slash == start) return false;
    const size_t length = (size_t)(slash - start);
    if (length >= size || length > 127U) return false;
    unsigned int components = 0U;
    bool in_component = false;
    for (size_t i = 0U; i < length; ++i) {
        const unsigned char ch = (unsigned char)start[i];
        if (ch >= '0' && ch <= '9') {
            in_component = true;
        } else if (ch == '.') {
            if (!in_component) return false;
            ++components;
            in_component = false;
        } else {
            return false;
        }
    }
    if (!in_component || components != 2U) return false;
    char expected[256];
    if (snprintf(expected, sizeof(expected), "%s%.*s%s",
                 asset_prefix, (int)length, start, asset_suffix) >= (int)sizeof(expected) ||
        strcmp(slash + 1, expected) != 0) return false;
    memcpy(output, start, length);
    output[length] = '\0';
    return true;
}
#endif
