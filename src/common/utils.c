/*
 * Rufux - Utility Functions Implementation
 * Francesco Lauritano
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "utils.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool command_exists(const char *cmd)
{
    char check[256];
    snprintf(check, sizeof(check), "which %s >/dev/null 2>&1", cmd);
    return system(check) == 0;
}

char *run_command(const char *cmd)
{
    FILE *fp = popen(cmd, "r");
    if (!fp)
        return NULL;

    char *output = NULL;
    size_t output_len = 0;
    char buf[4096];

    while (fgets(buf, sizeof(buf), fp)) {
        size_t len = strlen(buf);
        output = realloc(output, output_len + len + 1);
        if (!output) {
            pclose(fp);
            return NULL;
        }
        memcpy(output + output_len, buf, len + 1);
        output_len += len;
    }

    pclose(fp);
    return output;
}

int run_privileged(const char *cmd)
{
    if (is_root()) {
        return system(cmd);
    }

    const char *pkexec = get_pkexec_path();
    if (!pkexec) {
        rufus_error("pkexec not found, cannot run privileged command");
        return -1;
    }

    char full_cmd[4096];
    snprintf(full_cmd, sizeof(full_cmd), "%s %s", pkexec, cmd);
    return system(full_cmd);
}

bool is_root(void)
{
    return geteuid() == 0;
}

const char *get_pkexec_path(void)
{
    static const char *paths[] = {
        "/usr/bin/pkexec",
        "/bin/pkexec",
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        if (access(paths[i], X_OK) == 0)
            return paths[i];
    }

    return NULL;
}
