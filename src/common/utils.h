/*
 * Rufux - Utility Functions
 * Francesco Lauritano
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef RUFUS_UTILS_H
#define RUFUS_UTILS_H

#include <stdbool.h>
#include <stdint.h>

/* Check if a command exists in PATH */
bool command_exists(const char *cmd);

/* Run a command and capture output */
char *run_command(const char *cmd);

/* Run a command with privilege escalation */
int run_privileged(const char *cmd);

/* Check if running as root */
bool is_root(void);

/* Get the path to pkexec */
const char *get_pkexec_path(void);

#endif /* RUFUS_UTILS_H */
