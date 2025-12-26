/*
 * Rufux - Platform Abstraction Layer
 * Francesco Lauritano
 * Copyright (C) 2025
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Type definitions and abstractions for cross-platform compatibility.
 * Based on Rufus by Pete Batard.
 */

#ifndef RUFUS_PLATFORM_H
#define RUFUS_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>

/* Type definitions matching Windows types for easier porting */
typedef uint32_t DWORD;
typedef int64_t LONGLONG;
typedef uint64_t ULONGLONG;

/* Avoid conflicts with GLib's TRUE/FALSE */
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#define INVALID_HANDLE_VALUE (-1)

/* File system types */
typedef enum {
    FS_UNKNOWN = 0,
    FS_FAT16,
    FS_FAT32,
    FS_NTFS,
    FS_UDF,
    FS_EXFAT,
    FS_REFS,
    FS_EXT2,
    FS_EXT3,
    FS_EXT4,
    FS_MAX
} fs_type_t;

/* Partition styles */
typedef enum {
    PARTITION_STYLE_MBR = 0,
    PARTITION_STYLE_GPT,
} partition_style_t;

/* Target boot types */
typedef enum {
    TARGET_BIOS = 0,
    TARGET_UEFI,
    TARGET_BIOS_UEFI,
} target_type_t;

/* Boot selection types */
typedef enum {
    BOOT_NON_BOOTABLE = 0,
    BOOT_ISO_IMAGE,
    BOOT_DISK_IMAGE,
} boot_type_t;

/* Operation status */
typedef enum {
    OP_STATUS_IDLE = 0,
    OP_STATUS_RUNNING,
    OP_STATUS_SUCCESS,
    OP_STATUS_ERROR,
    OP_STATUS_CANCELLED,
} op_status_t;

/* Progress callback type */
typedef void (*progress_callback_t)(double fraction, const char *message, void *user_data);

/* Status callback type */
typedef void (*status_callback_t)(const char *message, op_status_t status, void *user_data);

/* String helper macros */
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#define safe_free(p) do { if (p) { free(p); (p) = NULL; } } while(0)
#define safe_strdup(s) ((s) ? strdup(s) : NULL)

/* Filesystem type names */
const char *fs_type_name(fs_type_t fs);
fs_type_t fs_type_from_name(const char *name);

/* Partition style names */
const char *partition_style_name(partition_style_t style);

/* Size formatting */
char *format_size(uint64_t bytes);

/* Logging */
void rufus_log(const char *format, ...);
void rufus_error(const char *format, ...);

#endif /* RUFUS_PLATFORM_H */
