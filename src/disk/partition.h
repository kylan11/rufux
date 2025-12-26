/*
 * Rufux - Partition Management
 * Francesco Lauritano
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef RUFUS_PARTITION_H
#define RUFUS_PARTITION_H

#include "../platform/platform.h"
#include <stdint.h>
#include <stdbool.h>

/* Partition entry for creation */
typedef struct {
    uint64_t start;       /* Start offset in bytes (0 = auto) */
    uint64_t size;        /* Size in bytes (0 = use remaining) */
    fs_type_t fs_type;    /* Filesystem type */
    bool bootable;        /* Set bootable flag (MBR only) */
    const char *label;    /* Partition label (GPT only) */
} partition_entry_t;

/* Partition layout */
typedef struct {
    partition_style_t style;  /* MBR or GPT */
    partition_entry_t *parts; /* Array of partitions */
    int part_count;           /* Number of partitions */
} partition_layout_t;

/* Create partition table on device
 * This will wipe all existing partitions!
 */
bool partition_create_table(const char *device, partition_style_t style);

/* Add a partition to the device */
bool partition_add(const char *device, const partition_entry_t *part, int part_number);

/* Create a simple single-partition layout (most common case) */
bool partition_create_single(const char *device, partition_style_t style,
                             fs_type_t fs_type, const char *label);

/* Create a single EFI System partition (FAT32) for UEFI boot */
bool partition_create_single_efi(const char *device, partition_style_t style,
                                 const char *label);

/* Create partition layout for bootable USB (with optional ESP for UEFI) */
bool partition_create_bootable(const char *device, partition_style_t style,
                               target_type_t target, fs_type_t fs_type,
                               const char *label);

/* Delete all partitions on device */
bool partition_delete_all(const char *device);

/* Get current partition layout */
partition_layout_t *partition_get_layout(const char *device);

/* Free partition layout */
void partition_layout_free(partition_layout_t *layout);

/* Get the path to a partition (e.g., "/dev/sda1") */
char *partition_get_path(const char *device, int part_number);

#endif /* RUFUS_PARTITION_H */
