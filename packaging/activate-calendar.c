// SPDX-License-Identifier: GPL-3.0-or-later
// Per-user, reversible Cinnamon Calendar panel integration, implemented in C.
// GSettings is accessed natively; no Python, shell parsing or root operations.
#define _DEFAULT_SOURCE 1
#include <gio/gio.h>
#include <fcntl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <json-c/json.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *APPLET = "/usr/share/cinnamon/applets/calendar@the-infiltratr/metadata.json";
static const char *NATIVE = "calendar@the-infiltratr";
static const char *STOCK = "calendar@cinnamon.org";
static const char *KEY = "enabled-applets";

static gboolean set_and_confirm(GSettings *settings, const gchar *const *expected) {
    if (!g_settings_set_strv(settings, KEY, expected)) return FALSE;
    g_settings_sync();
    gchar **actual = g_settings_get_strv(settings, KEY);
    gboolean same = g_strv_equal((const gchar *const *)actual, expected);
    g_strfreev(actual);
    return same;
}
static gboolean saved_is_valid(const gchar *text, GVariant **out) {
    GError *error = NULL;
    GVariant *saved = g_variant_parse(G_VARIANT_TYPE("as"), text, NULL, NULL, &error);
    if (error) {
        fprintf(stderr, "Invalid saved Cinnamon state: %s\n", error->message);
        g_error_free(error);
        return FALSE;
    }
    *out = saved;
    return saved != NULL;
}
static gboolean metadata_is_valid(void) {
    struct stat info;
    if (lstat(APPLET, &info) != 0 || !S_ISREG(info.st_mode)) return FALSE;
    gchar *bytes = NULL;
    gsize len = 0;
    if (!g_file_get_contents(APPLET, &bytes, &len, NULL)) return FALSE;
    json_tokener *token = json_tokener_new();
    json_object *meta = json_tokener_parse_ex(token, bytes, (int)len);
    gboolean valid = FALSE;
    if (meta && json_tokener_get_error(token) == json_tokener_success) {
        json_object *uuid = NULL;
        valid = json_object_object_get_ex(meta, "uuid", &uuid) &&
                json_object_get_type(uuid) == json_type_string &&
                g_strcmp0(json_object_get_string(uuid), NATIVE) == 0;
    }
    if (meta) json_object_put(meta);
    json_tokener_free(token);
    g_free(bytes);
    return valid;
}
static int calendar(int argc, char **argv) {
    gboolean dry_run = FALSE, restore = FALSE, activate = FALSE;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--dry-run")) dry_run = TRUE;
        else if (!strcmp(argv[i], "--restore")) restore = TRUE;
        else if (!strcmp(argv[i], "--activate")) activate = TRUE;
        else {
            fprintf(stderr, "Usage: activate-calendar [--dry-run] [--restore] [--activate]\n");
            return 2;
        }
    }
    if (geteuid() == 0) {
        fprintf(stderr, "Calendar panel integration must run as a Cinnamon user, not root\n");
        return 1;
    }
    GSettingsSchemaSource *src = g_settings_schema_source_get_default();
    GSettingsSchema *schema = src ? g_settings_schema_source_lookup(src, "org.cinnamon", TRUE) : NULL;
    if (!schema) {
        fprintf(stderr, "Cinnamon GSettings schema is not installed\n");
        return 1;
    }
    if (!g_settings_schema_has_key(schema, KEY)) {
        g_settings_schema_unref(schema);
        fprintf(stderr, "Cinnamon panel setting is unavailable\n");
        return 1;
    }
    GSettings *settings = g_settings_new_full(schema, NULL, NULL);
    g_settings_schema_unref(schema);
    gchar *state_base = g_strdup(g_getenv("XDG_STATE_HOME"));
    if (!state_base || !*state_base) {
        g_free(state_base);
        state_base = g_build_filename(g_get_home_dir(), ".local", "state", NULL);
    }
    gchar *state_dir = g_build_filename(state_base, "infiltratoros", NULL);
    gchar *backup = g_build_filename(state_dir, "calendar-panel.before", NULL);
    gchar *marker = g_build_filename(state_dir, "calendar-panel-migrated", NULL);
    gchar **original = g_settings_get_strv(settings, KEY);
    int result = 1;

    if (restore) {
        gchar *text = NULL;
        if (!g_file_get_contents(backup, &text, NULL, NULL)) {
            fprintf(stderr, "No saved calendar panel state exists\n");
            goto cleanup;
        }
        GVariant *saved = NULL;
        if (!saved_is_valid(text, &saved)) {
            g_free(text);
            goto cleanup;
        }
        g_free(text);
        gsize count = 0;
        const gchar **entries = g_variant_get_strv(saved, &count);
        (void)count;
        if (dry_run) {
            puts("Would restore the saved Cinnamon panel configuration");
            result = 0;
        } else if (set_and_confirm(settings, entries)) {
            if (g_mkdir_with_parents(state_dir, 0700) == 0 &&
                g_file_set_contents(marker, "Original calendar restored by user; leave unchanged\n", -1, NULL)) {
                puts("Original Cinnamon panel entries restored; auto-migration disabled");
                result = 0;
            }
        } else {
            fprintf(stderr, "Cinnamon did not accept the restored panel\n");
        }
        g_free(entries);
        g_variant_unref(saved);
        goto cleanup;
    }
    if (g_file_test(marker, G_FILE_TEST_IS_REGULAR) && !activate) {
        puts("Calendar migration already performed; respecting the user's panel changes");
        result = 0;
        goto cleanup;
    }
    if (!g_file_test(APPLET, G_FILE_TEST_EXISTS)) {
        puts("Infiltrator Calendar applet is not installed; stock calendar retained");
        result = 0;
        goto cleanup;
    }
    if (!metadata_is_valid()) {
        fprintf(stderr, "Installed Calendar applet metadata is invalid\n");
        goto cleanup;
    }
    gboolean already_native = FALSE;
    for (int i = 0; original[i]; ++i) {
        gchar **segments = g_strsplit(original[i], ":", -1);
        if (g_strv_length(segments) >= 4 && g_strcmp0(segments[3], NATIVE) == 0)
            already_native = TRUE;
        g_strfreev(segments);
    }
    if (already_native) {
        puts("Infiltrator Calendar already exists on the panel; leaving layout unchanged");
        result = 0;
        goto cleanup;
    }
    gchar **revised = g_strdupv(original);
    unsigned swapped = 0;
    for (int i = 0; revised[i]; ++i) {
        gchar **segments = g_strsplit(revised[i], ":", -1);
        if (g_strv_length(segments) >= 4 && g_strcmp0(segments[3], STOCK) == 0) {
            g_free(segments[3]);
            segments[3] = g_strdup(NATIVE);
            gchar *replacement = g_strjoinv(":", segments);
            g_free(revised[i]);
            revised[i] = replacement;
            ++swapped;
        }
        g_strfreev(segments);
    }
    if (!swapped) {
        puts("No stock Cinnamon Calendar on the panel; leaving layout unchanged");
        result = 0;
    } else if (dry_run) {
        printf("Would replace %u stock Calendar instance(s), preserving panel positions\n", swapped);
        result = 0;
    } else if (g_mkdir_with_parents(state_dir, 0700) == 0 && g_chmod(state_dir, 0700) == 0) {
        if (!g_file_test(backup, G_FILE_TEST_EXISTS)) {
            GVariant *old = g_settings_get_value(settings, KEY);
            gchar *printed = g_variant_print(old, TRUE);
            int fd = g_open(backup, O_WRONLY | O_CREAT | O_EXCL, 0600);
            if (fd < 0) {
                fprintf(stderr, "Cannot create Cinnamon backup\n");
                g_free(printed);
                g_variant_unref(old);
                g_strfreev(revised);
                goto cleanup;
            }
            gboolean written = write(fd, printed, strlen(printed)) == (ssize_t)strlen(printed);
            written = written && write(fd, "\n", 1) == 1;
            if (close(fd) != 0) written = FALSE;
            g_free(printed);
            g_variant_unref(old);
            if (!written) {
                fprintf(stderr, "Cannot write Cinnamon backup\n");
                g_strfreev(revised);
                goto cleanup;
            }
        }
        if (set_and_confirm(settings, (const gchar *const *)revised)) {
            if (g_file_set_contents(marker, "calendar@cinnamon.org -> calendar@the-infiltratr\n", -1, NULL)) {
                printf("Replaced stock Cinnamon Calendar in %u panel position(s)\n", swapped);
                puts("To undo: /System/Commands/InfiltratorOS/activate-calendar --restore");
                result = 0;
            } else set_and_confirm(settings, (const gchar *const *)original);
        } else {
            set_and_confirm(settings, (const gchar *const *)original);
            fprintf(stderr, "Cinnamon did not accept the Calendar replacement; original restored\n");
        }
    } else {
        fprintf(stderr, "Cannot create protected Cinnamon state directory\n");
    }
    g_strfreev(revised);
cleanup:
    g_strfreev(original);
    g_free(marker);
    g_free(backup);
    g_free(state_dir);
    g_free(state_base);
    g_object_unref(settings);
    return result;
}
int main(int argc, char **argv) { return calendar(argc, argv); }
