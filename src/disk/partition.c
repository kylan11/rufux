/*
 * Rufux - Partition Management Implementation
 * Francesco Lauritano
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Uses libfdisk for partition operations
 */

#include "partition.h"
#include "disk_io.h"
#include "../common/utils.h"
#include <libfdisk/libfdisk.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* MBR partition type codes */
#define MBR_TYPE_FAT16     0x06
#define MBR_TYPE_FAT32     0x0B
#define MBR_TYPE_FAT32_LBA 0x0C
#define MBR_TYPE_NTFS      0x07
#define MBR_TYPE_LINUX     0x83
#define MBR_TYPE_EFI       0xEF

/* GPT partition type GUIDs */
#define GPT_TYPE_EFI       "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"
#define GPT_TYPE_LINUX     "0FC63DAF-8483-4772-8E79-3D69D8477DE4"
#define GPT_TYPE_MSDATA    "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7"

static int get_mbr_type(fs_type_t fs)
{
    switch (fs) {
    case FS_FAT16:
        return MBR_TYPE_FAT16;
    case FS_FAT32:
        return MBR_TYPE_FAT32_LBA;
    case FS_NTFS:
    case FS_EXFAT:
        return MBR_TYPE_NTFS;
    case FS_EXT2:
    case FS_EXT3:
    case FS_EXT4:
        return MBR_TYPE_LINUX;
    default:
        return MBR_TYPE_FAT32_LBA;
    }
}

static const char *get_gpt_type(fs_type_t fs)
{
    switch (fs) {
    case FS_EXT2:
    case FS_EXT3:
    case FS_EXT4:
        return GPT_TYPE_LINUX;
    default:
        return GPT_TYPE_MSDATA;
    }
}

static bool partition_create_single_privileged(const char *device, partition_style_t style,
                                               fs_type_t fs_type)
{
    if (!command_exists("sfdisk")) {
        rufus_error("sfdisk not found; cannot create partitions");
        return false;
    }

    const char *label = (style == PARTITION_STYLE_GPT) ? "gpt" : "dos";
    const char *boot_flag = (style == PARTITION_STYLE_MBR) ? ", *" : "";

    char type[64];
    if (style == PARTITION_STYLE_GPT) {
        snprintf(type, sizeof(type), "%s", get_gpt_type(fs_type));
    } else {
        snprintf(type, sizeof(type), "%02X", get_mbr_type(fs_type));
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "sh -c 'printf \"label: %s\\n, , %s%s\\n\" | "
             "sfdisk --wipe always --wipe-partitions always --lock %s'",
             label, type, boot_flag, device);

    int rc = run_privileged(cmd);
    if (rc != 0) {
        rufus_error("sfdisk failed to partition %s", device);
        return false;
    }

    return true;
}

static bool partition_create_single_efi_privileged(const char *device, partition_style_t style)
{
    if (!command_exists("sfdisk")) {
        rufus_error("sfdisk not found; cannot create EFI partition");
        return false;
    }

    const char *label = (style == PARTITION_STYLE_GPT) ? "gpt" : "dos";
    const char *boot_flag = (style == PARTITION_STYLE_MBR) ? ", *" : "";

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "sh -c 'printf \"label: %s\\n, , U%s\\n\" | "
             "sfdisk --wipe always --wipe-partitions always --lock %s'",
             label, boot_flag, device);

    int rc = run_privileged(cmd);
    if (rc != 0) {
        rufus_error("sfdisk failed to create EFI partition on %s", device);
        return false;
    }

    return true;
}

static bool partition_create_bootable_privileged(const char *device, partition_style_t style,
                                                 target_type_t target, fs_type_t fs_type)
{
    if (!command_exists("sfdisk")) {
        rufus_error("sfdisk not found; cannot create partitions");
        return false;
    }

    if (style != PARTITION_STYLE_GPT || target == TARGET_BIOS) {
        return partition_create_single_privileged(device, style, fs_type);
    }

    const char *data_type = get_gpt_type(fs_type);

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "sh -c 'printf \"label: gpt\\n, 256M, U\\n, , %s\\n\" | "
             "sfdisk --wipe always --wipe-partitions always --lock %s'",
             data_type, device);

    int rc = run_privileged(cmd);
    if (rc != 0) {
        rufus_error("sfdisk failed to partition %s", device);
        return false;
    }

    return true;
}

bool partition_create_table(const char *device, partition_style_t style)
{
    struct fdisk_context *cxt = fdisk_new_context();
    if (!cxt) {
        rufus_error("Failed to create fdisk context");
        return false;
    }

    if (fdisk_assign_device(cxt, device, 0) != 0) {
        rufus_error("Failed to assign device %s", device);
        fdisk_unref_context(cxt);
        return false;
    }

    /* Create label (partition table) */
    const char *label_type = (style == PARTITION_STYLE_GPT) ? "gpt" : "dos";
    struct fdisk_label *lb = fdisk_get_label(cxt, label_type);
    if (!lb) {
        rufus_error("Failed to get %s label", label_type);
        fdisk_deassign_device(cxt, 0);
        fdisk_unref_context(cxt);
        return false;
    }

    if (fdisk_create_disklabel(cxt, label_type) != 0) {
        rufus_error("Failed to create %s partition table", label_type);
        fdisk_deassign_device(cxt, 0);
        fdisk_unref_context(cxt);
        return false;
    }

    /* Write to disk */
    if (fdisk_write_disklabel(cxt) != 0) {
        rufus_error("Failed to write partition table");
        fdisk_deassign_device(cxt, 0);
        fdisk_unref_context(cxt);
        return false;
    }

    fdisk_deassign_device(cxt, 1); /* 1 = sync */
    fdisk_unref_context(cxt);

    rufus_log("Created %s partition table on %s", label_type, device);
    return true;
}

bool partition_add(const char *device, const partition_entry_t *part, int part_number)
{
    struct fdisk_context *cxt = fdisk_new_context();
    if (!cxt)
        return false;

    if (fdisk_assign_device(cxt, device, 0) != 0) {
        fdisk_unref_context(cxt);
        return false;
    }

    struct fdisk_partition *pa = fdisk_new_partition();
    if (!pa) {
        fdisk_deassign_device(cxt, 0);
        fdisk_unref_context(cxt);
        return false;
    }

    /* Set partition number */
    fdisk_partition_set_partno(pa, part_number - 1);

    /* Set start (default to first available if 0) */
    if (part->start > 0) {
        uint64_t sector_size = fdisk_get_sector_size(cxt);
        fdisk_partition_set_start(pa, part->start / sector_size);
    } else {
        fdisk_partition_start_follow_default(pa, 1);
    }

    /* Set size (use all remaining if 0) */
    if (part->size > 0) {
        uint64_t sector_size = fdisk_get_sector_size(cxt);
        fdisk_partition_set_size(pa, part->size / sector_size);
    } else {
        fdisk_partition_end_follow_default(pa, 1);
    }

    /* Set partition type */
    struct fdisk_parttype *type = NULL;
    if (fdisk_is_label(cxt, DOS)) {
        type = fdisk_label_get_parttype_from_code(fdisk_get_label(cxt, NULL),
                                                   get_mbr_type(part->fs_type));
    } else if (fdisk_is_label(cxt, GPT)) {
        type = fdisk_label_get_parttype_from_string(fdisk_get_label(cxt, NULL),
                                                     get_gpt_type(part->fs_type));
    }
    if (type) {
        fdisk_partition_set_type(pa, type);
        fdisk_unref_parttype(type);
    }

    /* Add partition */
    size_t partno;
    if (fdisk_add_partition(cxt, pa, &partno) != 0) {
        rufus_error("Failed to add partition");
        fdisk_unref_partition(pa);
        fdisk_deassign_device(cxt, 0);
        fdisk_unref_context(cxt);
        return false;
    }

    /* Set bootable flag for MBR */
    if (part->bootable && fdisk_is_label(cxt, DOS)) {
        fdisk_toggle_partition_flag(cxt, partno, DOS_FLAG_ACTIVE);
    }

    /* Write changes */
    if (fdisk_write_disklabel(cxt) != 0) {
        rufus_error("Failed to write partition");
        fdisk_unref_partition(pa);
        fdisk_deassign_device(cxt, 0);
        fdisk_unref_context(cxt);
        return false;
    }

    fdisk_unref_partition(pa);
    fdisk_deassign_device(cxt, 1);
    fdisk_unref_context(cxt);

    rufus_log("Added partition %d to %s", part_number, device);
    return true;
}

static bool partition_add_efi(const char *device, const partition_entry_t *part, int part_number)
{
    struct fdisk_context *cxt = fdisk_new_context();
    if (!cxt)
        return false;

    if (fdisk_assign_device(cxt, device, 0) != 0) {
        fdisk_unref_context(cxt);
        return false;
    }

    struct fdisk_partition *pa = fdisk_new_partition();
    if (!pa) {
        fdisk_deassign_device(cxt, 0);
        fdisk_unref_context(cxt);
        return false;
    }

    fdisk_partition_set_partno(pa, part_number - 1);

    if (part->start > 0) {
        uint64_t sector_size = fdisk_get_sector_size(cxt);
        fdisk_partition_set_start(pa, part->start / sector_size);
    } else {
        fdisk_partition_start_follow_default(pa, 1);
    }

    if (part->size > 0) {
        uint64_t sector_size = fdisk_get_sector_size(cxt);
        fdisk_partition_set_size(pa, part->size / sector_size);
    } else {
        fdisk_partition_end_follow_default(pa, 1);
    }

    struct fdisk_parttype *type = NULL;
    if (fdisk_is_label(cxt, DOS)) {
        type = fdisk_label_get_parttype_from_code(fdisk_get_label(cxt, NULL), MBR_TYPE_EFI);
    } else if (fdisk_is_label(cxt, GPT)) {
        type = fdisk_label_get_parttype_from_string(fdisk_get_label(cxt, NULL), GPT_TYPE_EFI);
    }
    if (type) {
        fdisk_partition_set_type(pa, type);
        fdisk_unref_parttype(type);
    }

    size_t partno;
    if (fdisk_add_partition(cxt, pa, &partno) != 0) {
        rufus_error("Failed to add EFI partition");
        fdisk_unref_partition(pa);
        fdisk_deassign_device(cxt, 0);
        fdisk_unref_context(cxt);
        return false;
    }

    if (part->bootable && fdisk_is_label(cxt, DOS)) {
        fdisk_toggle_partition_flag(cxt, partno, DOS_FLAG_ACTIVE);
    }

    if (fdisk_write_disklabel(cxt) != 0) {
        rufus_error("Failed to write EFI partition");
        fdisk_unref_partition(pa);
        fdisk_deassign_device(cxt, 0);
        fdisk_unref_context(cxt);
        return false;
    }

    fdisk_unref_partition(pa);
    fdisk_deassign_device(cxt, 1);
    fdisk_unref_context(cxt);

    rufus_log("Added EFI partition %d to %s", part_number, device);
    return true;
}

bool partition_create_single(const char *device, partition_style_t style,
                             fs_type_t fs_type, const char *label)
{
    if (!is_root()) {
        (void)label;
        return partition_create_single_privileged(device, style, fs_type);
    }

    if (!partition_create_table(device, style))
        return false;

    /* Wait for kernel to recognize new partition table */
    usleep(500000);

    partition_entry_t part = {
        .start = 0,
        .size = 0, /* Use all space */
        .fs_type = fs_type,
        .bootable = true,
        .label = label,
    };

    return partition_add(device, &part, 1);
}

bool partition_create_single_efi(const char *device, partition_style_t style,
                                 const char *label)
{
    if (!is_root()) {
        (void)label;
        return partition_create_single_efi_privileged(device, style);
    }

    if (!partition_create_table(device, style))
        return false;

    usleep(500000);

    partition_entry_t part = {
        .start = 0,
        .size = 0,
        .fs_type = FS_FAT32,
        .bootable = true,
        .label = label,
    };

    return partition_add_efi(device, &part, 1);
}

bool partition_create_bootable(const char *device, partition_style_t style,
                               target_type_t target, fs_type_t fs_type,
                               const char *label)
{
    if (!is_root()) {
        (void)label;
        return partition_create_bootable_privileged(device, style, target, fs_type);
    }

    /* For UEFI, we might need an ESP partition */
    if (target == TARGET_UEFI || target == TARGET_BIOS_UEFI) {
        if (style == PARTITION_STYLE_GPT) {
            /* Create GPT with ESP + main partition */
            if (!partition_create_table(device, style))
                return false;

            usleep(500000);

            /* EFI System Partition (256 MB) */
            partition_entry_t esp = {
                .start = 0,
                .size = 256 * 1024 * 1024,
                .fs_type = FS_FAT32,
                .bootable = false,
                .label = "EFI",
            };

            if (!partition_add(device, &esp, 1))
                return false;

            usleep(200000);

            /* Main partition */
            partition_entry_t main_part = {
                .start = 0, /* Auto after ESP */
                .size = 0,  /* Use remaining */
                .fs_type = fs_type,
                .bootable = false,
                .label = label,
            };

            return partition_add(device, &main_part, 2);
        }
    }

    /* Simple single partition for BIOS or MBR */
    return partition_create_single(device, style, fs_type, label);
}

bool partition_delete_all(const char *device)
{
    /* Just create a new empty partition table */
    /* First try to detect current style, default to MBR */
    struct fdisk_context *cxt = fdisk_new_context();
    if (!cxt)
        return false;

    if (fdisk_assign_device(cxt, device, 1) != 0) {
        fdisk_unref_context(cxt);
        return partition_create_table(device, PARTITION_STYLE_MBR);
    }

    partition_style_t style = PARTITION_STYLE_MBR;
    if (fdisk_is_label(cxt, GPT))
        style = PARTITION_STYLE_GPT;

    fdisk_deassign_device(cxt, 0);
    fdisk_unref_context(cxt);

    return partition_create_table(device, style);
}

partition_layout_t *partition_get_layout(const char *device)
{
    struct fdisk_context *cxt = fdisk_new_context();
    if (!cxt)
        return NULL;

    if (fdisk_assign_device(cxt, device, 1) != 0) {
        fdisk_unref_context(cxt);
        return NULL;
    }

    partition_layout_t *layout = calloc(1, sizeof(partition_layout_t));
    if (!layout) {
        fdisk_deassign_device(cxt, 0);
        fdisk_unref_context(cxt);
        return NULL;
    }

    if (fdisk_is_label(cxt, GPT))
        layout->style = PARTITION_STYLE_GPT;
    else
        layout->style = PARTITION_STYLE_MBR;

    struct fdisk_table *tb = NULL;
    if (fdisk_get_partitions(cxt, &tb) == 0 && tb) {
        size_t n = fdisk_table_get_nents(tb);
        if (n > 0) {
            layout->parts = calloc(n, sizeof(partition_entry_t));
            layout->part_count = 0;

            struct fdisk_iter *itr = fdisk_new_iter(FDISK_ITER_FORWARD);
            struct fdisk_partition *pa = NULL;

            while (fdisk_table_next_partition(tb, itr, &pa) == 0) {
                if (fdisk_partition_has_start(pa) && fdisk_partition_has_size(pa)) {
                    uint64_t sector_size = fdisk_get_sector_size(cxt);
                    layout->parts[layout->part_count].start =
                        fdisk_partition_get_start(pa) * sector_size;
                    layout->parts[layout->part_count].size =
                        fdisk_partition_get_size(pa) * sector_size;
                    layout->part_count++;
                }
            }
            fdisk_free_iter(itr);
        }
        fdisk_unref_table(tb);
    }

    fdisk_deassign_device(cxt, 0);
    fdisk_unref_context(cxt);

    return layout;
}

void partition_layout_free(partition_layout_t *layout)
{
    if (!layout)
        return;
    free(layout->parts);
    free(layout);
}

char *partition_get_path(const char *device, int part_number)
{
    char *path = malloc(strlen(device) + 16);
    if (!path)
        return NULL;

    /* Handle /dev/sdX vs /dev/nvme0n1 naming */
    const char *base = strrchr(device, '/');
    base = base ? base + 1 : device;
    size_t len = strlen(base);
    bool needs_p = false;

    if (strncmp(base, "nvme", 4) == 0 ||
        strncmp(base, "mmcblk", 6) == 0 ||
        strncmp(base, "loop", 4) == 0) {
        needs_p = true;
    } else if (len > 0 && isdigit((unsigned char)base[len - 1])) {
        needs_p = true;
    }

    if (needs_p) {
        /* nvme/mmc/loop style: /dev/nvme0n1p1 */
        sprintf(path, "%sp%d", device, part_number);
    } else {
        /* sd style: /dev/sda1 */
        sprintf(path, "%s%d", device, part_number);
    }

    return path;
}
