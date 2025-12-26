/*
 * Rufux - Main Window
 * Francesco Lauritano
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef RUFUS_WINDOW_H
#define RUFUS_WINDOW_H

#include <gtk/gtk.h>
#include "app.h"

#define RUFUS_TYPE_WINDOW (rufus_window_get_type())

G_DECLARE_FINAL_TYPE(RufusWindow, rufus_window, RUFUS, WINDOW, GtkApplicationWindow)

RufusWindow *rufus_window_new(RufusApp *app);

#endif /* RUFUS_WINDOW_H */
