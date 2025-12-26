/*
 * Rufux - Platform Abstraction Implementation
 * Francesco Lauritano
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static const char *fs_names[] = {
    [FS_UNKNOWN] = "Unknown",
    [FS_FAT16] = "FAT16",
    [FS_FAT32] = "FAT32",
    [FS_NTFS] = "NTFS",
    [FS_UDF] = "UDF",
    [FS_EXFAT] = "exFAT",
    [FS_REFS] = "ReFS",
    [FS_EXT2] = "ext2",
    [FS_EXT3] = "ext3",
    [FS_EXT4] = "ext4",
};

const char *fs_type_name(fs_type_t fs)
{
    if (fs >= FS_MAX)
        return "Unknown";
    return fs_names[fs];
}

fs_type_t fs_type_from_name(const char *name)
{
    if (!name)
        return FS_UNKNOWN;

    for (int i = 0; i < FS_MAX; i++) {
        if (strcasecmp(name, fs_names[i]) == 0)
            return (fs_type_t)i;
    }
    return FS_UNKNOWN;
}

const char *partition_style_name(partition_style_t style)
{
    switch (style) {
    case PARTITION_STYLE_MBR:
        return "MBR";
    case PARTITION_STYLE_GPT:
        return "GPT";
    default:
        return "Unknown";
    }
}

char *format_size(uint64_t bytes)
{
    char *result = malloc(32);
    if (!result)
        return NULL;

    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size = (double)bytes;

    while (size >= 1024.0 && unit < 4) {
        size /= 1024.0;
        unit++;
    }

    if (unit == 0)
        snprintf(result, 32, "%lu %s", (unsigned long)bytes, units[unit]);
    else
        snprintf(result, 32, "%.1f %s", size, units[unit]);

    return result;
}

void rufus_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    fprintf(stdout, "[rufus] ");
    vfprintf(stdout, format, args);
    fprintf(stdout, "\n");
    va_end(args);
}

void rufus_error(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[rufus ERROR] ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}
