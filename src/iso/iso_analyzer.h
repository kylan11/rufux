/*
 * Rufux - ISO Analysis
 * Francesco Lauritano
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Detect ISO bootability, type, and metadata
 */

#ifndef RUFUS_ISO_ANALYZER_H
#define RUFUS_ISO_ANALYZER_H

#include "../platform/platform.h"
#include <stdbool.h>
#include <stdint.h>

/* Boot type detection */
typedef enum {
    BOOT_TYPE_UNKNOWN = 0,
    BOOT_TYPE_BIOS,
    BOOT_TYPE_UEFI,
    BOOT_TYPE_HYBRID,  /* Both BIOS and UEFI */
} iso_boot_type_t;

/* ISO information structure */
typedef struct {
    char *path;              /* Path to ISO file */
    char *label;             /* Volume label */
    uint64_t size;           /* File size in bytes */
    iso_boot_type_t boot_type;  /* Detected boot type */
    bool is_bootable;        /* Is the ISO bootable? */
    bool has_efi;            /* Has EFI directory */
    bool has_eltorito;       /* Has El Torito boot catalog */
    bool is_hybrid;          /* Is hybrid ISO (can be dd'd) */
    bool is_windows;         /* Appears to be Windows ISO */
    bool is_linux;           /* Appears to be Linux ISO */
} iso_info_t;

/* Analyze an ISO file */
iso_info_t *iso_analyze(const char *path);

/* Free ISO info structure */
void iso_info_free(iso_info_t *info);

/* Get boot type as string */
const char *iso_boot_type_name(iso_boot_type_t type);

/* Check if ISO is bootable */
bool iso_is_bootable(const char *path);

/* Get ISO volume label */
char *iso_get_label(const char *path);

/* Get ISO size */
uint64_t iso_get_size(const char *path);

#endif /* RUFUS_ISO_ANALYZER_H */
