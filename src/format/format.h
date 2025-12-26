/*
 * Rufux - Filesystem Formatting
 * Francesco Lauritano
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef RUFUS_FORMAT_H
#define RUFUS_FORMAT_H

#include "../platform/platform.h"
#include <stdbool.h>
#include <stdint.h>

/* Format options */
typedef struct {
    fs_type_t fs_type;      /* Filesystem type */
    const char *label;       /* Volume label */
    uint32_t cluster_size;   /* Cluster size (0 = default) */
    bool quick_format;       /* Quick format (no bad block check) */
} format_options_t;

/* Format progress callback */
typedef void (*format_progress_t)(double fraction, const char *message, void *user_data);

/* Format a partition
 * partition_path: e.g., "/dev/sda1"
 * options: format options
 * progress: optional progress callback
 * user_data: passed to callback
 */
bool format_partition(const char *partition_path, const format_options_t *options,
                      format_progress_t progress, void *user_data);

/* Check if mkfs tool exists for a given filesystem */
bool format_is_supported(fs_type_t fs_type);

/* Get the mkfs command for a filesystem */
const char *format_get_mkfs_command(fs_type_t fs_type);

/* Synchronous format with optional label setting */
bool format_sync(const char *partition_path, fs_type_t fs_type,
                 const char *label, uint32_t cluster_size);

#endif /* RUFUS_FORMAT_H */
