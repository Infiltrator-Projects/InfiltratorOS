#include <gtk/gtk.h>
#include "installer-profile.h"
#include "installer-status.h"
#include <dirent.h>
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

/* One progress window follows the complete 58-component installation run.
 * Individual native workers keep independent status/log files and the existing
 * global build lock; no additional GUI processes are spawned for providers. */
static gboolean refresh_aggregate(Ui *ui)
{
    enum { TOTAL_JOBS = 58 };
    DIR *directory = opendir(ui->status_path);
    if (directory == NULL) {
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(ui->bar));
        return G_SOURCE_CONTINUE;
    }
    int complete = 0, failed = 0, queued = 0, running = 0;
    double fraction = 0.0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        const char *name = entry->d_name;
        const size_t length = strlen(name);
        const char suffix[] = "-install.status";
        const size_t suffix_length = sizeof(suffix) - 1U;
        if (length <= suffix_length ||
            strcmp(name + length - suffix_length, suffix) != 0) continue;
        char path[4096];
        if (snprintf(path, sizeof(path), "%s/%s", ui->status_path, name) >= (int)sizeof(path))
            continue;
        InstallerStatus job;
        if (!installer_status_read(path, &job)) continue;
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
        } else {
            ++queued;
        }
    }
    closedir(directory);
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > TOTAL_JOBS) fraction = TOTAL_JOBS;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui->bar),
                                  fraction / (double)TOTAL_JOBS);
    char stage[256], detail[512];
    const int finished = complete + failed;
    snprintf(stage, sizeof(stage), "%d of %d installations finished",
             finished, TOTAL_JOBS);
    snprintf(detail, sizeof(detail),
             "%d complete, %d failed, %d running, %d queued. "
             "Per-job diagnostics are retained in /Temporary/InfiltratorOS.",
             complete, failed, running, queued);
    gtk_label_set_text(GTK_LABEL(ui->stage), stage);
    gtk_label_set_text(GTK_LABEL(ui->detail), detail);
    if (finished == TOTAL_JOBS) {
        gtk_label_set_text(GTK_LABEL(ui->title), failed ?
            "InfiltratorOS installation finished with failures" :
            "InfiltratorOS native installation is complete");
        gtk_widget_show(ui->close_button);
        if (!failed && ui->close_timer_id == 0U)
            ui->close_timer_id = g_timeout_add_seconds(8U, close_after_success, ui);
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

int main(int argc, char **argv)
{
    const gboolean aggregate = argc == 3 && strcmp(argv[1], "--aggregate") == 0;
#ifdef INSTALL_TEMPORAL_RESOURCES
    if (!aggregate && (argc != 3 ||
        strlen(argv[2]) >= sizeof(((Ui *)0)->product_name))) return EXIT_FAILURE;
#else
    if (!aggregate && argc != 2) return EXIT_FAILURE;
#endif
    if (strlen(argv[aggregate ? 2 : 1]) >= sizeof(((Ui *)0)->status_path))
        return EXIT_FAILURE;
    gtk_init(&argc, &argv);

    /* Product typography, resolved by Fontconfig from the verified shared
     * /System/Resources/Fonts cache. Retained Title faces keep a standalone
     * host readable if the newer Text/Screen faces are not yet present. */
    GtkCssProvider *typography = gtk_css_provider_new();
    gtk_css_provider_load_from_data(typography,
        "window, label, button, progressbar {"
        " font-family: 'MB Corpo S Text WEB', 'MB Corpo S Title WEB';"
        " font-weight: 400;"
        "}"
        "#installer-title {"
        " font-family: 'MB Corpo S Title WEB', 'MB Corpo S Text WEB';"
        " font-size: 19px; font-weight: 700;"
        "}"
        "#installer-stage {"
        " font-family: 'MB Corpo S Text WEB Cond', 'MB Corpo S Text WEB';"
        " font-size: 14px;"
        "}"
        "#installer-detail { font-size: 13px; }"
        "#installer-progress text {"
        " font-family: 'MB Corpo A Screen2 Cond WEB', 'MB Corpo A Title Cond WEB';"
        " font-size: 14px; font-weight: 400;"
        "}", -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(typography),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(typography);

    Ui ui;
    memset(&ui, 0, sizeof(ui));
    ui.aggregate = aggregate;
    g_strlcpy(ui.status_path, argv[aggregate ? 2 : 1], sizeof(ui.status_path));
#ifdef INSTALL_TEMPORAL_RESOURCES
    g_strlcpy(ui.product_name, aggregate ? "InfiltratorOS" : argv[2],
              sizeof(ui.product_name));
#else
    g_strlcpy(ui.product_name, aggregate ? "InfiltratorOS" : INSTALL_NAME,
              sizeof(ui.product_name));
#endif

    ui.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ui.window), "InfiltratorOS");
    gtk_window_set_default_size(GTK_WINDOW(ui.window), 520, 190);
    gtk_window_set_resizable(GTK_WINDOW(ui.window), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(ui.window), 18);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(ui.window), box);

    ui.title = gtk_label_new(NULL);
    gtk_widget_set_name(ui.title, "installer-title");
    char initial_title[256];
    snprintf(initial_title, sizeof(initial_title), "Optimising %s for this computer", ui.product_name);
    gtk_label_set_text(GTK_LABEL(ui.title), initial_title);
    gtk_widget_set_halign(ui.title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), ui.title, FALSE, FALSE, 0);

    ui.stage = gtk_label_new("Preparing native installation…");
    gtk_widget_set_name(ui.stage, "installer-stage");
    gtk_widget_set_halign(ui.stage, GTK_ALIGN_START);
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
