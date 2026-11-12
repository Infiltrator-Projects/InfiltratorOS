#include <gtk/gtk.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-font.h>
#include <pango/pangofc-fontmap.h>
#include <fontconfig/fontconfig.h>
#include "infiltratr/design.h"
#include "installer-profile.h"
#include "installer-status.h"
#include "native-progress-count.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    GtkWidget *window;
    GtkWidget *title;
    GtkWidget *stage;
    GtkWidget *detail;
    GtkWidget *bar;
    GtkWidget *close_button;
    char status_path[4096];
    char product_name[128];
    gboolean aggregate;
    guint close_timer_id;
} Ui;

static void cancel_success_close(Ui *ui)
{
    if (ui->close_timer_id != 0U) {
        g_source_remove(ui->close_timer_id);
        ui->close_timer_id = 0U;
    }
}

static gboolean close_after_success(gpointer data)
{
    Ui *ui = data;
    ui->close_timer_id = 0U;
    if (GTK_IS_WIDGET(ui->window)) gtk_widget_destroy(ui->window);
    return G_SOURCE_REMOVE;
}

/* The run outcome remains after successful cleanup of the per-run manifests.
 * Match the recorded stage exactly; never read another installation's result. */
static int finished_run_outcome(const char *status_path)
{
    FILE *file = fopen("/Temporary/InfiltratorOS/last-provision.status", "r");
    if (!file) return 0;
    char state[64], stage[4096], expected[4096];
    const int valid = fgets(state, sizeof(state), file) != NULL &&
                      fgets(stage, sizeof(stage), file) != NULL;
    (void)fclose(file);
    if (!valid || strncmp(stage, "stage=", 6) != 0) return 0;
    const size_t length = strlen(stage);
    if (length < 8 || stage[length - 1] != '\n') return 0;
    stage[length - 1] = '\0';
    if (snprintf(expected, sizeof(expected), "%s/status", stage + 6) >=
            (int)sizeof(expected) || strcmp(expected, status_path) != 0) return 0;
    if (strcmp(state, "state=complete\n") == 0) return 1;
    if (strcmp(state, "state=failed\n") == 0) return 2;
    if (strcmp(state, "state=timeout\n") == 0) return 3;
    return 0;
}

static gboolean refresh_aggregate(Ui *ui)
{
    const int outcome = finished_run_outcome(ui->status_path);
    if (outcome != 0) {
        gtk_label_set_text(GTK_LABEL(ui->title),
            outcome == 1 ? "InfiltratorOS native installation is complete" :
            outcome == 2 ? "InfiltratorOS installation finished with failures" :
                           "InfiltratorOS installation timed out");
        gtk_label_set_text(GTK_LABEL(ui->stage),
            outcome == 1 ? "All eligible native jobs completed" :
                           "Native provisioning needs attention");
        gtk_label_set_text(GTK_LABEL(ui->detail),
            outcome == 1 ? "The run outcome is recorded in /Temporary/InfiltratorOS." :
                           "The run outcome and retained diagnostics are in /Temporary/InfiltratorOS.");
        if (outcome == 1) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui->bar), 1.0);
        gtk_widget_show(ui->close_button);
        if (outcome == 1 && ui->close_timer_id == 0U)
            ui->close_timer_id = g_timeout_add_seconds(8U, close_after_success, ui);
        if (outcome != 1) cancel_success_close(ui);
        return G_SOURCE_CONTINUE;
    }
    NativeProgressJobs jobs;
    const int total_jobs = native_progress_load_jobs(ui->status_path, &jobs);
    /* The lists are written before the job workers begin. Never infer a
     * smaller fixed total while the manifests are unavailable. */
    if (total_jobs <= 0) {
        cancel_success_close(ui);
        gtk_label_set_text(GTK_LABEL(ui->title),
            "Preparing InfiltratorOS installation inventory");
        gtk_label_set_text(GTK_LABEL(ui->stage),
            "Reading exact native installation manifests");
        gtk_label_set_text(GTK_LABEL(ui->detail),
            "Waiting for the provider manifests for this installation run.");
        gtk_widget_hide(ui->close_button);
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(ui->bar));
        return G_SOURCE_CONTINUE;
    }
    int complete = 0, failed = 0, queued = 0, running = 0;
    double fraction = 0.0;
    char active_name[96] = {0}, active_stage[128] = {0};
    /* Read only the signed run inventory. An unexpected status file must
     * never substitute for a missing expected worker or close the UI early. */
    for (int index = 0; index < jobs.count; ++index) {
        const char *name = jobs.slugs[index];
        char path[4096];
        if (snprintf(path, sizeof(path), "%s/%s-install.status",
                     ui->status_path, name) >= (int)sizeof(path)) {
            ++queued;
            continue;
        }
        InstallerStatus job;
        if (!installer_status_read(path, &job)) {
            ++queued;
            continue;
        }
        if (strcmp(job.state, "complete") == 0) {
            ++complete;
            fraction += 1.0;
        } else if (strcmp(job.state, "failed") == 0) {
            ++failed;
            fraction += 1.0;
        } else if (strcmp(job.state, "running") == 0) {
            ++running;
            if (job.progress >= 0 && job.progress <= 100)
                fraction += (double)job.progress / 100.0;
            if (active_name[0] == '\0') {
                snprintf(active_name, sizeof(active_name), "%.95s", name);
                snprintf(active_stage, sizeof(active_stage), "%.110s", job.stage);
            }
        } else {
            ++queued;
        }
    }
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > total_jobs) fraction = total_jobs;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui->bar),
                                  fraction / (double)total_jobs);
    char stage[256], detail[512];
    const int finished = complete + failed;
    snprintf(stage, sizeof(stage), "%d of %d installations finished",
             finished, total_jobs);
    if (running && active_name[0]) {
        snprintf(detail, sizeof(detail),
                 "%d complete, %d failed, %d running, %d queued. Current: %s — %s. "
                 "Per-job diagnostics: /Temporary/InfiltratorOS.",
                 complete, failed, running, queued, active_name, active_stage);
    } else {
        snprintf(detail, sizeof(detail),
                 "%d complete, %d failed, %d running, %d queued. "
                 "Per-job diagnostics: /Temporary/InfiltratorOS.",
                 complete, failed, running, queued);
    }
    gtk_label_set_text(GTK_LABEL(ui->stage), stage);
    gtk_label_set_text(GTK_LABEL(ui->detail), detail);
    if (finished == total_jobs) {
        /* All workers are terminal, but the authoritative post-install monitor
         * must still publish and qualify the run before we announce success. */
        cancel_success_close(ui);
        gtk_label_set_text(GTK_LABEL(ui->title),
            "Finalising InfiltratorOS installation");
        gtk_label_set_text(GTK_LABEL(ui->stage),
            "Waiting for the verified final installation result");
        gtk_widget_hide(ui->close_button);
    } else {
        cancel_success_close(ui);
        gtk_label_set_text(GTK_LABEL(ui->title),
            failed ? "InfiltratorOS installation needs attention" :
                     "Installing InfiltratorOS native components");
        gtk_widget_hide(ui->close_button);
    }
    return G_SOURCE_CONTINUE;
}

static gboolean refresh_status(gpointer data)
{
    Ui *ui = data;
    if (ui->aggregate) return refresh_aggregate(ui);
    InstallerStatus status;
    if (!installer_status_read(ui->status_path, &status)) {
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(ui->bar));
        return G_SOURCE_CONTINUE;
    }

    if (status.progress >= 0 && status.progress <= 100)
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui->bar), (double)status.progress / 100.0);
    else
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(ui->bar));

    if (status.stage[0]) gtk_label_set_text(GTK_LABEL(ui->stage), status.stage);
    if (status.detail[0]) gtk_label_set_text(GTK_LABEL(ui->detail), status.detail);

    if (strcmp(status.state, "complete") == 0) {
        char title[256];
        snprintf(title, sizeof(title), "%s is ready", ui->product_name);
        gtk_label_set_text(GTK_LABEL(ui->title), title);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui->bar), 1.0);
        gtk_widget_show(ui->close_button);
        if (ui->close_timer_id == 0U)
            ui->close_timer_id = g_timeout_add_seconds(8U, close_after_success, ui);
    } else if (strcmp(status.state, "failed") == 0) {
        cancel_success_close(ui);
        char title[256];
        snprintf(title, sizeof(title), "%s installation failed", ui->product_name);
        gtk_label_set_text(GTK_LABEL(ui->title), title);
        gtk_widget_show(ui->close_button);
    } else {
        cancel_success_close(ui);
        char title[256];
        snprintf(title, sizeof(title), "Optimising %s for this computer", ui->product_name);
        gtk_label_set_text(GTK_LABEL(ui->title), title);
        gtk_widget_hide(ui->close_button);
    }
    return G_SOURCE_CONTINUE;
}

static void close_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    Ui *ui = data;
    cancel_success_close(ui);
    gtk_widget_destroy(ui->window);
}


/* Check the files that Pango actually selects for the roles in the GTK CSS. */
static bool required_fonts_visible(const char *directory)
{
    static const struct {
        const char *family;
        const char *file;
        PangoWeight weight;
    } roles[] = {
        {"MB Corpo A Title Cond WEB", "mb_corpo_a_title_cond_regular.ttf", PANGO_WEIGHT_NORMAL},
        {"MB Corpo S Title WEB", "mb_corpo_s_title_bold.ttf", PANGO_WEIGHT_BOLD},
        {"MB Corpo S Text WEB", "mb_corpo_s_text_regular.ttf", PANGO_WEIGHT_NORMAL},
        {"MB Corpo S Text WEB Cond", "mb_corpo_s_text_cond_regular.ttf", PANGO_WEIGHT_NORMAL},
        {"MB Corpo A Screen2 Cond WEB", "mb_corpo_a_screen_2_cond_regular.ttf", PANGO_WEIGHT_NORMAL}
    };
    FcConfig *config = FcConfigGetCurrent();
    PangoFontMap *map = pango_cairo_font_map_get_default();
    if (!config || !PANGO_IS_FC_FONT_MAP(map) ||
        !FcConfigAppFontAddDir(config, (const FcChar8 *)directory))
        return false;
    pango_fc_font_map_config_changed(PANGO_FC_FONT_MAP(map));
    PangoContext *context = pango_font_map_create_context(map);
    PangoFontDescription *description = pango_font_description_new();
    pango_font_description_set_size(description, 12 * PANGO_SCALE);
    bool valid = true;
    for (size_t i = 0; i < G_N_ELEMENTS(roles); ++i) {
        pango_font_description_set_family(description, roles[i].family);
        pango_font_description_set_weight(description, roles[i].weight);
        PangoFont *font = pango_font_map_load_font(map, context, description);
        FcChar8 *file = NULL;
        char *expected = g_build_filename(directory, roles[i].file, NULL);
        valid = PANGO_IS_FC_FONT(font) &&
                FcPatternGetString(pango_fc_font_get_pattern(PANGO_FC_FONT(font)),
                                   FC_FILE, 0, &file) == FcResultMatch &&
                strcmp((const char *)file, expected) == 0;
        g_free(expected);
        if (font) g_object_unref(font);
        if (!valid) {
            fprintf(stderr, "InfiltratorOS: %s did not resolve to %s.\n",
                    roles[i].family, roles[i].file);
            break;
        }
    }
    pango_font_description_free(description);
    g_object_unref(context);
    return valid;
}

static bool system_prefers_dark(void)
{
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    if (source) {
        GSettingsSchema *schema =
            g_settings_schema_source_lookup(source, "org.gnome.desktop.interface", TRUE);
        if (schema) {
            if (g_settings_schema_has_key(schema, "color-scheme")) {
                GSettings *settings = g_settings_new("org.gnome.desktop.interface");
                gchar *scheme = g_settings_get_string(settings, "color-scheme");
                bool dark = g_strcmp0(scheme, "prefer-dark") == 0;
                bool light = g_strcmp0(scheme, "prefer-light") == 0;
                g_free(scheme);
                g_object_unref(settings);
                g_settings_schema_unref(schema);
                if (dark || light) return dark;
            } else {
                g_settings_schema_unref(schema);
            }
        }
    }
    GtkSettings *settings = gtk_settings_get_default();
    if (!settings) return false;
    gboolean prefer_dark = FALSE;
    gchar *theme_name = NULL;
    g_object_get(settings, "gtk-application-prefer-dark-theme", &prefer_dark,
                 "gtk-theme-name", &theme_name, NULL);
    gchar *lower = theme_name ? g_ascii_strdown(theme_name, -1) : NULL;
    bool dark = prefer_dark || (lower && strstr(lower, "dark") != NULL);
    g_free(lower);
    g_free(theme_name);
    return dark;
}

static bool install_common_theme(void)
{
    const InfiltratrThemePalette *palette =
        infiltratr_theme_resolve(INFILTRATR_THEME_SYSTEM, system_prefers_dark());
    const InfiltratrDesignMetrics *metrics = infiltratr_design_metrics();
    const InfiltratrTypography *typography = infiltratr_typography();
    if (!infiltratr_theme_palette_is_valid(palette) ||
        !infiltratr_design_metrics_is_valid(metrics) ||
        !infiltratr_typography_is_valid(typography)) return false;
    char *css = g_strdup_printf(
        "window { background-color:#%06x; color:#%06x; }"
        "label { color:#%06x; font-family:'MB Corpo S Text WEB'; font-weight:400; }"
        "#installer-brand { font-family:'%s'; color:#%06x; font-size:17px; font-weight:400; }"
        "#installer-title { font-family:'MB Corpo S Title WEB'; color:#%06x;"
        " font-size:19px; font-weight:700; }"
        "#installer-stage { font-family:'MB Corpo S Text WEB Cond'; color:#%06x; font-size:14px; }"
        "#installer-detail, #installer-subtitle { color:#%06x; font-size:13px; }"
        "progressbar trough { background-color:#%06x; border:1px solid #%06x;"
        " border-radius:%upx; min-height:15px; }"
        "progressbar progress { background-color:#%06x; border-radius:%upx; }"
        "progressbar text { font-family:'MB Corpo A Screen2 Cond WEB'; color:#%06x; font-size:14px; }"
        "button { background-color:#%06x; color:#%06x; border:1px solid #%06x;"
        " border-radius:%upx; padding:6px 12px; }"
        "button:hover { background-color:#%06x; }"
        "button:focus { outline:2px solid #%06x; }",
        palette->background_rgb, palette->text_rgb,
        palette->text_rgb, typography->brand_family, palette->title_rgb,
        palette->heading_rgb, palette->summary_rgb, palette->muted_rgb,
        palette->surface_rgb, palette->border_rgb, metrics->control_radius,
        palette->neutral_accent_rgb, metrics->control_radius,
        palette->text_rgb, palette->button_background_rgb, palette->button_foreground_rgb,
        palette->border_rgb, metrics->control_radius, palette->equals_hover_rgb,
        palette->neutral_accent_rgb);
    if (!css) return false;
    GtkCssProvider *provider = gtk_css_provider_new();
    GError *error = NULL;
    gtk_css_provider_load_from_data(provider, css, -1, &error);
    g_free(css);
    if (error) {
        g_error_free(error);
        g_object_unref(provider);
        return false;
    }
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
    return true;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--check-fonts") == 0)
        return required_fonts_visible(argv[2]) ? EXIT_SUCCESS : EXIT_FAILURE;
    if (argc == 3 && strcmp(argv[1], "--run-status") == 0) {
        puts(native_progress_run_status(argv[2]));
        return EXIT_SUCCESS;
    }
    const gboolean aggregate = argc == 3 && strcmp(argv[1], "--aggregate") == 0;
#if defined(INSTALL_TEMPORAL_RESOURCES) || defined(INSTALL_FILESYSTEM_RESOURCES)
    if (!aggregate && (argc != 3 ||
        strlen(argv[2]) >= sizeof(((Ui *)0)->product_name))) return EXIT_FAILURE;
#else
    if (!aggregate && argc != 2) return EXIT_FAILURE;
#endif
    if (strlen(argv[aggregate ? 2 : 1]) >= sizeof(((Ui *)0)->status_path))
        return EXIT_FAILURE;
    gtk_init(&argc, &argv);

    /* Common owns semantic colours, and unresolved fonts are an error. */
    if (!required_fonts_visible("/System/Resources/Fonts") || !install_common_theme()) {
        fprintf(stderr, "InfiltratorOS: verified fonts or Common theme unavailable.\n");
        GtkWidget *error = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
            "InfiltratorOS cannot resolve its verified fonts or Common theme. "
            "See /Temporary/InfiltratorOS for installation details.");
        gtk_dialog_run(GTK_DIALOG(error));
        gtk_widget_destroy(error);
        return EXIT_FAILURE;
    }

    Ui ui;
    memset(&ui, 0, sizeof(ui));
    ui.aggregate = aggregate;
    g_strlcpy(ui.status_path, argv[aggregate ? 2 : 1], sizeof(ui.status_path));
#if defined(INSTALL_TEMPORAL_RESOURCES) || defined(INSTALL_FILESYSTEM_RESOURCES)
    g_strlcpy(ui.product_name, aggregate ? "InfiltratorOS" : argv[2],
              sizeof(ui.product_name));
#else
    g_strlcpy(ui.product_name, aggregate ? "InfiltratorOS" : INSTALL_NAME,
              sizeof(ui.product_name));
#endif

    ui.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ui.window), "InfiltratorOS " INFILTRATOROS_VERSION);
    gtk_window_set_default_size(GTK_WINDOW(ui.window), 600, 240);
    gtk_window_set_resizable(GTK_WINDOW(ui.window), TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(ui.window),
        infiltratr_design_metrics()->screen_padding);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(ui.window), box);

    GtkWidget *brand_header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(box), brand_header, FALSE, FALSE, 0);

    GtkWidget *brand = gtk_label_new("InfiltratorOS");
    gtk_widget_set_name(brand, "installer-brand");
    gtk_widget_set_halign(brand, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(brand_header), brand, FALSE, FALSE, 0);

    GtkWidget *subtitle = gtk_label_new("Upgrader for Debian-based systems");
    gtk_widget_set_name(subtitle, "installer-subtitle");
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(brand_header), subtitle, FALSE, FALSE, 0);

    ui.title = gtk_label_new(NULL);
    gtk_widget_set_name(ui.title, "installer-title");
    char initial_title[256];
    snprintf(initial_title, sizeof(initial_title), "Optimising %s for this computer", ui.product_name);
    gtk_label_set_text(GTK_LABEL(ui.title), initial_title);
    gtk_widget_set_halign(ui.title, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(ui.title), TRUE);
    gtk_label_set_xalign(GTK_LABEL(ui.title), 0.0F);
    gtk_box_pack_start(GTK_BOX(box), ui.title, FALSE, FALSE, 0);

    ui.stage = gtk_label_new("Preparing native installation…");
    gtk_widget_set_name(ui.stage, "installer-stage");
    gtk_widget_set_halign(ui.stage, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(ui.stage), TRUE);
    gtk_label_set_xalign(GTK_LABEL(ui.stage), 0.0F);
    gtk_box_pack_start(GTK_BOX(box), ui.stage, FALSE, FALSE, 0);

    ui.bar = gtk_progress_bar_new();
    gtk_widget_set_name(ui.bar, "installer-progress");
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(ui.bar), TRUE);
    gtk_box_pack_start(GTK_BOX(box), ui.bar, FALSE, FALSE, 0);

    ui.detail = gtk_label_new("You can keep using the computer while this finishes.");
    gtk_widget_set_name(ui.detail, "installer-detail");
    gtk_label_set_line_wrap(GTK_LABEL(ui.detail), TRUE);
    gtk_label_set_xalign(GTK_LABEL(ui.detail), 0.0F);
    gtk_box_pack_start(GTK_BOX(box), ui.detail, FALSE, FALSE, 0);

    ui.close_button = gtk_button_new_with_label("Close");
    gtk_widget_set_halign(ui.close_button, GTK_ALIGN_END);
    g_signal_connect(ui.close_button, "clicked", G_CALLBACK(close_clicked), &ui);
    gtk_box_pack_end(GTK_BOX(box), ui.close_button, FALSE, FALSE, 0);
    gtk_widget_hide(ui.close_button);

    g_signal_connect(ui.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_show_all(ui.window);
    gtk_widget_hide(ui.close_button);

    g_timeout_add(250U, refresh_status, &ui);
    (void)refresh_status(&ui);
    gtk_main();
    return EXIT_SUCCESS;
}
