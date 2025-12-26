/*
 * Rufux - GTK4 Application Implementation
 * Francesco Lauritano
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "app.h"
#include "window.h"

#define RUFUS_VERSION "0.1.0"

struct _RufusApp {
    GtkApplication parent_instance;
};

G_DEFINE_TYPE(RufusApp, rufus_app, GTK_TYPE_APPLICATION)

static void on_about_action(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;

    GtkApplication *app = GTK_APPLICATION(user_data);
    GtkWindow *window = gtk_application_get_active_window(app);

    const char *authors[] = {
        "Rufux Contributors",
        NULL
    };

    GtkAboutDialog *about = GTK_ABOUT_DIALOG(gtk_about_dialog_new());
    gtk_about_dialog_set_program_name(about, "Rufux");
    gtk_about_dialog_set_version(about, RUFUS_VERSION);
    gtk_about_dialog_set_comments(about, "Create bootable USB drives");
    gtk_about_dialog_set_license_type(about, GTK_LICENSE_GPL_3_0);
    gtk_about_dialog_set_website(about, "https://github.com/pbatard/rufus");
    gtk_about_dialog_set_website_label(about, "Rufus Project");
    gtk_about_dialog_set_authors(about, authors);
    gtk_about_dialog_set_copyright(about, "Based on Rufus by Pete Batard");

    gtk_window_set_transient_for(GTK_WINDOW(about), window);
    gtk_window_set_modal(GTK_WINDOW(about), TRUE);
    gtk_window_present(GTK_WINDOW(about));
}

static void rufus_app_activate(GApplication *app)
{
    GtkWindow *window;

    window = gtk_application_get_active_window(GTK_APPLICATION(app));
    if (window == NULL) {
        window = GTK_WINDOW(rufus_window_new(RUFUS_APP(app)));
    }

    gtk_window_present(window);
}

static void rufus_app_startup(GApplication *app)
{
    G_APPLICATION_CLASS(rufus_app_parent_class)->startup(app);

    /* Register actions */
    static const GActionEntry app_actions[] = {
        { "about", on_about_action, NULL, NULL, NULL, { 0 } },
    };
    g_action_map_add_action_entries(G_ACTION_MAP(app), app_actions,
                                     G_N_ELEMENTS(app_actions), app);

    /* Set up keyboard shortcuts */
    const char *about_accels[] = { "F1", NULL };
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.about", about_accels);

    /* Load CSS */
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(provider, "/org/rufus/linux/style.css");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static void rufus_app_class_init(RufusAppClass *klass)
{
    GApplicationClass *app_class = G_APPLICATION_CLASS(klass);

    app_class->activate = rufus_app_activate;
    app_class->startup = rufus_app_startup;
}

static void rufus_app_init(RufusApp *app)
{
    (void)app;
}

RufusApp *rufus_app_new(void)
{
    return g_object_new(RUFUS_TYPE_APP,
                        "application-id", "org.rufus.linux",
                        "flags", G_APPLICATION_DEFAULT_FLAGS,
                        NULL);
}
