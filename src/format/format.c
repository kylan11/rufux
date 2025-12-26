/*
 * Rufux - Filesystem Formatting Implementation
 * Francesco Lauritano
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Uses system mkfs.* tools for formatting
 */

#include "format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

/* mkfs command info structure */
typedef struct {
    fs_type_t type;
    const char *command;
    const char *label_opt;
    const char *cluster_opt;
} mkfs_info_t;

/* mkfs command mapping */
static const mkfs_info_t mkfs_commands[] = {
    { FS_FAT16,  "mkfs.fat",   "-n", "-s" },
    { FS_FAT32,  "mkfs.fat",   "-n", "-s" },
    { FS_NTFS,   "mkfs.ntfs",  "-L", "-c" },
    { FS_EXFAT,  "mkfs.exfat", "-L", "-s" },
    { FS_EXT2,   "mkfs.ext2",  "-L", "-b" },
    { FS_EXT3,   "mkfs.ext3",  "-L", "-b" },
    { FS_EXT4,   "mkfs.ext4",  "-L", "-b" },
    { FS_UDF,    "mkudffs",    "-l", NULL },
};

static const mkfs_info_t *get_mkfs_info(fs_type_t type)
{
    for (size_t i = 0; i < sizeof(mkfs_commands) / sizeof(mkfs_commands[0]); i++) {
        if (mkfs_commands[i].type == type)
            return &mkfs_commands[i];
    }
    return NULL;
}

bool format_is_supported(fs_type_t fs_type)
{
    const char *cmd = format_get_mkfs_command(fs_type);
    if (!cmd)
        return false;

    /* Check if command exists in PATH */
    char check_cmd[256];
    snprintf(check_cmd, sizeof(check_cmd), "which %s >/dev/null 2>&1", cmd);
    return system(check_cmd) == 0;
}

const char *format_get_mkfs_command(fs_type_t fs_type)
{
    const mkfs_info_t *info = get_mkfs_info(fs_type);
    return info ? info->command : NULL;
}

static char **build_mkfs_args(const char *partition_path, const format_options_t *opts,
                               int *argc)
{
    const mkfs_info_t *info = get_mkfs_info(opts->fs_type);
    if (!info)
        return NULL;

    /* Allocate space for arguments */
    char **args = calloc(16, sizeof(char *));
    if (!args)
        return NULL;

    int n = 0;

    /* Command */
    args[n++] = strdup(info->command);

    /* FAT-specific: specify FAT type */
    if (opts->fs_type == FS_FAT16) {
        args[n++] = strdup("-F");
        args[n++] = strdup("16");
    } else if (opts->fs_type == FS_FAT32) {
        args[n++] = strdup("-F");
        args[n++] = strdup("32");
    }

    /* NTFS: quick format */
    if (opts->fs_type == FS_NTFS && opts->quick_format) {
        args[n++] = strdup("-Q");
    }

    /* Label */
    if (opts->label && opts->label[0] && info->label_opt) {
        args[n++] = strdup(info->label_opt);
        args[n++] = strdup(opts->label);
    }

    /* Cluster size */
    if (opts->cluster_size > 0 && info->cluster_opt) {
        args[n++] = strdup(info->cluster_opt);
        char size_str[32];
        if (opts->fs_type == FS_FAT16 || opts->fs_type == FS_FAT32) {
            /* FAT uses sectors per cluster */
            snprintf(size_str, sizeof(size_str), "%u", opts->cluster_size / 512);
        } else {
            /* Others use bytes */
            snprintf(size_str, sizeof(size_str), "%u", opts->cluster_size);
        }
        args[n++] = strdup(size_str);
    }

    /* Device path */
    args[n++] = strdup(partition_path);

    /* NULL terminate */
    args[n] = NULL;

    *argc = n;
    return args;
}

static void free_args(char **args)
{
    if (!args)
        return;
    for (int i = 0; args[i]; i++)
        free(args[i]);
    free(args);
}

bool format_partition(const char *partition_path, const format_options_t *options,
                      format_progress_t progress, void *user_data)
{
    if (!partition_path || !options) {
        rufus_error("Invalid arguments to format_partition");
        return false;
    }

    if (!format_is_supported(options->fs_type)) {
        rufus_error("Filesystem %s is not supported (mkfs tool not found)",
                   fs_type_name(options->fs_type));
        return false;
    }

    int argc;
    char **args = build_mkfs_args(partition_path, options, &argc);
    if (!args) {
        rufus_error("Failed to build mkfs arguments");
        return false;
    }

    /* Log the command */
    char cmd_str[1024] = "";
    for (int i = 0; args[i]; i++) {
        if (i > 0)
            strcat(cmd_str, " ");
        strcat(cmd_str, args[i]);
    }
    rufus_log("Running: %s", cmd_str);

    if (progress)
        progress(0.0, "Starting format...", user_data);

    /* Fork and exec */
    pid_t pid = fork();
    if (pid < 0) {
        rufus_error("Failed to fork: %s", strerror(errno));
        free_args(args);
        return false;
    }

    if (pid == 0) {
        /* Child process */
        /* Redirect stdout/stderr to /dev/null for clean output */
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);

        /* If not root, use pkexec for privilege escalation */
        if (geteuid() != 0) {
            /* Rebuild args with pkexec prepended */
            char **new_args = calloc(argc + 2, sizeof(char *));
            new_args[0] = "pkexec";
            for (int i = 0; i < argc; i++) {
                new_args[i + 1] = args[i];
            }
            new_args[argc + 1] = NULL;
            execvp("pkexec", new_args);
        } else {
            execvp(args[0], args);
        }
        _exit(127);
    }

    /* Parent process */
    free_args(args);

    /* Wait for completion with simulated progress */
    int status;
    int elapsed = 0;
    while (waitpid(pid, &status, WNOHANG) == 0) {
        usleep(100000); /* 100ms */
        elapsed++;
        if (progress) {
            /* Simulate progress (formatting is usually fast) */
            double frac = elapsed < 50 ? elapsed * 0.02 : 0.99;
            progress(frac, "Formatting...", user_data);
        }
    }

    if (progress)
        progress(1.0, "Complete", user_data);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        rufus_log("Format completed successfully");
        return true;
    }

    rufus_error("Format failed with exit code %d",
               WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    return false;
}

bool format_sync(const char *partition_path, fs_type_t fs_type,
                 const char *label, uint32_t cluster_size)
{
    format_options_t opts = {
        .fs_type = fs_type,
        .label = label,
        .cluster_size = cluster_size,
        .quick_format = true,
    };

    return format_partition(partition_path, &opts, NULL, NULL);
}
