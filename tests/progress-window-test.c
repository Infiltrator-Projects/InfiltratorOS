// SPDX-License-Identifier: GPL-3.0-or-later
/* Inspect the real packaged GTK widgets after their first refresh under Xvfb. */
#include <gtk/gtk.h>
#include <math.h>
#include <stdlib.h>

static GtkWidget *find_named(GtkWidget *widget, const char *name)
{
    if (g_strcmp0(gtk_widget_get_name(widget), name) == 0) return widget;
    if (!GTK_IS_CONTAINER(widget)) return NULL;
    GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
    GtkWidget *match = NULL;
    for (GList *item = children; item && !match; item = item->next)
        match = find_named(item->data, name);
    g_list_free(children);
    return match;
}

static void check(gboolean condition, const char *message)
{
    if (!condition) {
        g_printerr("Progress window: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static double linear_channel(double value)
{
    return value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
}

static double luminance(const GdkRGBA *colour)
{
    return 0.2126 * linear_channel(colour->red) + 0.7152 * linear_channel(colour->green) +
           0.0722 * linear_channel(colour->blue);
}

void gtk_main(void)
{
    GList *windows = gtk_window_list_toplevels();
    check(windows != NULL && GTK_IS_WINDOW(windows->data), "no GTK window");
    GtkWidget *window = windows->data;
    g_list_free(windows);
    check(gtk_widget_get_visible(window), "upgrader window hidden");
    GtkWidget *subtitle = find_named(window, "installer-subtitle");
    check(GTK_IS_LABEL(subtitle) && gtk_widget_get_visible(subtitle) &&
          g_strcmp0(gtk_label_get_text(GTK_LABEL(subtitle)),
                    "Upgrader for Debian-based systems") == 0, "missing visible upgrader subtitle");
    GtkWidget *percent = find_named(window, "installer-percent");
    GtkWidget *close = find_named(window, "installer-close");
    check(GTK_IS_LABEL(percent) && GTK_IS_BUTTON(close), "missing progress or Close widget");
    if (g_strcmp0(g_getenv("PROGRESS_EXPECT"), "preparing") == 0) {
        check(!gtk_widget_get_visible(percent) && !gtk_widget_get_visible(close),
              "preparation should show a pulse without 0% or a Close button");
    } else {
        check(gtk_widget_get_visible(percent) && gtk_widget_get_visible(close) &&
              g_strcmp0(gtk_label_get_text(GTK_LABEL(percent)), "100%") == 0,
              "completed state should show 100% and Close");
        GtkWidget *label = gtk_bin_get_child(GTK_BIN(close));
        check(GTK_IS_LABEL(label) && gtk_widget_get_visible(label), "Close label hidden");
        GtkStyleContext *fg_style = gtk_widget_get_style_context(label);
        GtkStyleContext *bg_style = gtk_widget_get_style_context(close);
        GdkRGBA fg, *bg = NULL;
        gtk_style_context_get_color(fg_style, gtk_style_context_get_state(fg_style), &fg);
        gtk_style_context_get(bg_style, gtk_style_context_get_state(bg_style),
                              "background-color", &bg, NULL);
        check(bg && bg->alpha > 0.99 && fg.alpha > 0.99, "Close colours must be opaque");
        const double a = luminance(&fg), b = luminance(bg);
        const double contrast = (MAX(a, b) + 0.05) / (MIN(a, b) + 0.05);
        gdk_rgba_free(bg);
        g_print("Close label contrast %.2f:1\n", contrast);
        check(contrast >= 4.5, "Close label contrast is below 4.5:1");
    }
    exit(EXIT_SUCCESS);
}
