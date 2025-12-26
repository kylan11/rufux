/*
 * Rufux - GTK4 Application
 * Francesco Lauritano
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef RUFUS_APP_H
#define RUFUS_APP_H

#include <gtk/gtk.h>

#define RUFUS_TYPE_APP (rufus_app_get_type())

G_DECLARE_FINAL_TYPE(RufusApp, rufus_app, RUFUS, APP, GtkApplication)

RufusApp *rufus_app_new(void);

#endif /* RUFUS_APP_H */
