/*
 * Rufux - ISO Extraction Implementation
 * Francesco Lauritano
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _GNU_SOURCE
#include "iso_extract.h"
#include "../common/utils.h"
#include "../platform/platform.h"
#include <glib.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *select_extract_tool(void)
{
    if (command_exists("xorriso"))
        return "xorriso";
    if (command_exists("bsdtar"))
        return "bsdtar";
    if (command_exists("7z"))
        return "7z";
    return NULL;
}

const char *iso_extract_tool_name(void)
{
    return select_extract_tool();
}

bool iso_extract_is_supported(void)
{
    return select_extract_tool() != NULL;
}

static char *build_extract_command(const char *tool, const char *iso_path, const char *mount_dir)
{
    char *q_iso = g_shell_quote(iso_path);
    char *cmd = NULL;

    if (strcmp(tool, "xorriso") == 0) {
        cmd = g_strdup_printf("xorriso -osirrox on -indev %s -extract / %s",
                              q_iso, mount_dir);
    } else if (strcmp(tool, "bsdtar") == 0) {
        cmd = g_strdup_printf("bsdtar -C %s -xf %s", mount_dir, q_iso);
    } else if (strcmp(tool, "7z") == 0) {
        cmd = g_strdup_printf("7z x -y -o%s %s", mount_dir, q_iso);
    }

    g_free(q_iso);
    return cmd;
}

bool iso_extract_to_partition(const char *iso_path, const char *partition_path,
                              iso_extract_progress_t progress, void *user_data)
{
    if (!iso_path || !partition_path) {
        rufus_error("Invalid arguments to iso_extract_to_partition");
        return false;
    }

    const char *tool = select_extract_tool();
    if (!tool) {
        rufus_error("No ISO extraction tool found (xorriso, bsdtar, or 7z)");
        return false;
    }

    struct stat st;
    if (stat(iso_path, &st) != 0) {
        rufus_error("Cannot stat ISO file: %s", strerror(errno));
        return false;
    }

    char mount_template[] = "/tmp/rufus-mount-XXXXXX";
    char *mount_dir = mkdtemp(mount_template);
    if (!mount_dir) {
        rufus_error("Failed to create mount directory: %s", strerror(errno));
        return false;
    }

    char *extract_cmd = build_extract_command(tool, iso_path, mount_dir);
    if (!extract_cmd) {
        rufus_error("Failed to build extract command");
        rmdir(mount_dir);
        return false;
    }

    rufus_log("Extracting ISO using %s", tool);

    char *script = g_strdup_printf(
        "set -e; "
        "mount %s %s; "
        "trap 'umount %s' EXIT; "
        "%s; "
        "sync",
        partition_path, mount_dir, mount_dir, extract_cmd);

    char *cmd = g_strdup_printf("sh -c \"%s\"", script);

    if (progress)
        progress(0.0, "Extracting ISO...", user_data);

    int rc = run_privileged(cmd);

    if (progress)
        progress(rc == 0 ? 1.0 : 0.0, rc == 0 ? "Complete" : "Failed", user_data);

    g_free(cmd);
    g_free(script);
    g_free(extract_cmd);

    if (rmdir(mount_dir) != 0) {
        rufus_log("Warning: failed to remove mount dir %s", mount_dir);
    }

    return rc == 0;
}
