/*
 * Rufux - Main Entry Point
 * Francesco Lauritano
 * Copyright (C) 2025
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A Linux port of Rufus, the reliable USB formatting utility.
 * Based on Rufus by Pete Batard.
 */

#include <gtk/gtk.h>
#include <locale.h>
#include "ui/app.h"
#include "platform/platform.h"

int main(int argc, char *argv[])
{
    /* Set up locale */
    setlocale(LC_ALL, "");

    rufus_log("Rufux starting...");

    /* Create and run application */
    RufusApp *app = rufus_app_new();
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
