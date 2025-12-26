/*
 * Rufux - Device Enumeration
 * Francesco Lauritano
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * USB device detection using libudev and /sys/block
 */

#ifndef RUFUS_DEVICE_H
#define RUFUS_DEVICE_H

#include "../platform/platform.h"
#include <stdbool.h>
#include <stdint.h>

/* Maximum number of devices to enumerate */
#define MAX_DEVICES 64

/* Device information structure */
typedef struct {
    char *name;           /* Device name (e.g., "sda") */
    char *path;           /* Full path (e.g., "/dev/sda") */
    char *vendor;         /* Vendor string */
    char *model;          /* Model string */
    char *serial;         /* Serial number */
    uint64_t size;        /* Size in bytes */
    uint16_t vid;         /* USB Vendor ID */
    uint16_t pid;         /* USB Product ID */
    bool removable;       /* Is removable media */
    bool is_usb;          /* Is USB device */
    char *bus_type;       /* Bus type (usb, sata, nvme, etc.) */
    char **mountpoints;   /* Array of mountpoints (NULL-terminated) */
    int mountpoint_count; /* Number of mountpoints */
} device_info_t;

/* Device list structure */
typedef struct {
    device_info_t *devices;
    int count;
} device_list_t;

/* Enumerate USB devices
 * Returns a device_list_t that must be freed with device_list_free()
 * Only returns USB devices that are safe to write to (not system drives)
 */
device_list_t *device_enumerate(void);

/* Free a device list */
void device_list_free(device_list_t *list);

/* Get display name for a device (e.g., "SanDisk Cruzer (8.0 GB)") */
char *device_display_name(const device_info_t *dev);

/* Check if device has any mounted partitions */
bool device_is_mounted(const device_info_t *dev);

/* Unmount all partitions on a device */
bool device_unmount(const device_info_t *dev);

/* Check if device contains system partitions (/, /boot, /home) */
bool device_is_system_drive(const device_info_t *dev);

/* Refresh device list (call when USB devices change) */
device_list_t *device_refresh(void);

/* Start device monitoring (calls callback on USB insert/remove) */
typedef void (*device_change_callback_t)(void *user_data);
bool device_monitor_start(device_change_callback_t callback, void *user_data);
void device_monitor_stop(void);

#endif /* RUFUS_DEVICE_H */
