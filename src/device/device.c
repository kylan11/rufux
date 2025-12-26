/*
 * Rufux - Device Enumeration Implementation
 * Francesco Lauritano
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * USB device detection using libudev
 */

#include "device.h"
#include <libudev.h>
#include <blkid/blkid.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/mount.h>
#include <mntent.h>
#include <pthread.h>

/* Forbidden mountpoints - never allow writing to devices with these */
static const char *forbidden_mounts[] = {
    "/",
    "/boot",
    "/boot/efi",
    "/home",
    NULL
};

static struct udev_monitor *udev_mon = NULL;
static pthread_t monitor_thread;
static bool monitor_running = false;
static device_change_callback_t change_callback = NULL;
static void *callback_user_data = NULL;

/* Helper: duplicate a string safely */
static char *safe_strdup_trim(const char *s)
{
    if (!s || !*s)
        return NULL;

    /* Trim whitespace */
    while (*s == ' ')
        s++;

    size_t len = strlen(s);
    while (len > 0 && s[len - 1] == ' ')
        len--;

    if (len == 0)
        return NULL;

    char *result = malloc(len + 1);
    if (result) {
        memcpy(result, s, len);
        result[len] = '\0';
    }
    return result;
}

/* Get mountpoints for a device from /proc/mounts */
static char **get_mountpoints(const char *device_path, int *count)
{
    FILE *fp = setmntent("/proc/mounts", "r");
    if (!fp) {
        *count = 0;
        return NULL;
    }

    char **mounts = NULL;
    int n = 0;
    struct mntent *ent;

    /* Get base device name for partition matching */
    const char *base = strrchr(device_path, '/');
    base = base ? base + 1 : device_path;

    while ((ent = getmntent(fp)) != NULL) {
        /* Check if this mount is on our device or its partitions */
        const char *mnt_base = strrchr(ent->mnt_fsname, '/');
        mnt_base = mnt_base ? mnt_base + 1 : ent->mnt_fsname;

        /* Match device or partitions (e.g., sda matches sda, sda1, sda2) */
        if (strncmp(mnt_base, base, strlen(base)) == 0) {
            mounts = realloc(mounts, (n + 2) * sizeof(char *));
            if (mounts) {
                mounts[n] = strdup(ent->mnt_dir);
                mounts[n + 1] = NULL;
                n++;
            }
        }
    }

    endmntent(fp);
    *count = n;
    return mounts;
}

/* Free mountpoints array */
static void free_mountpoints(char **mounts)
{
    if (!mounts)
        return;
    for (int i = 0; mounts[i]; i++)
        free(mounts[i]);
    free(mounts);
}

/* Check if any mountpoint is forbidden */
static bool has_forbidden_mount(char **mounts)
{
    if (!mounts)
        return false;

    for (int i = 0; mounts[i]; i++) {
        for (int j = 0; forbidden_mounts[j]; j++) {
            if (strcmp(mounts[i], forbidden_mounts[j]) == 0)
                return true;
            /* Also check if mountpoint starts with /home/ */
            if (strncmp(mounts[i], "/home/", 6) == 0)
                return true;
        }
    }
    return false;
}

/* Read a sysfs attribute */
static char *read_sysfs_attr(const char *path, const char *attr)
{
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", path, attr);

    FILE *fp = fopen(filepath, "r");
    if (!fp)
        return NULL;

    char buf[256];
    if (!fgets(buf, sizeof(buf), fp)) {
        fclose(fp);
        return NULL;
    }
    fclose(fp);

    /* Remove trailing newline */
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
        buf[len - 1] = '\0';

    return strdup(buf);
}

/* Get device size from sysfs */
static uint64_t get_device_size(const char *name)
{
    char path[256];
    snprintf(path, sizeof(path), "/sys/block/%s", name);

    char *size_str = read_sysfs_attr(path, "size");
    if (!size_str)
        return 0;

    uint64_t sectors = strtoull(size_str, NULL, 10);
    free(size_str);

    /* Sectors are 512 bytes */
    return sectors * 512;
}

/* Check if device is removable */
static bool is_removable(const char *name)
{
    char path[256];
    snprintf(path, sizeof(path), "/sys/block/%s", name);

    char *rm_str = read_sysfs_attr(path, "removable");
    if (!rm_str)
        return false;

    bool result = (rm_str[0] == '1');
    free(rm_str);
    return result;
}

device_list_t *device_enumerate(void)
{
    struct udev *udev = udev_new();
    if (!udev) {
        rufus_error("Failed to create udev context");
        return NULL;
    }

    device_list_t *list = calloc(1, sizeof(device_list_t));
    if (!list) {
        udev_unref(udev);
        return NULL;
    }

    list->devices = calloc(MAX_DEVICES, sizeof(device_info_t));
    if (!list->devices) {
        free(list);
        udev_unref(udev);
        return NULL;
    }

    struct udev_enumerate *enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enumerate, "block");
    udev_enumerate_add_match_property(enumerate, "DEVTYPE", "disk");
    udev_enumerate_scan_devices(enumerate);

    struct udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
    struct udev_list_entry *entry;

    udev_list_entry_foreach(entry, devices) {
        if (list->count >= MAX_DEVICES)
            break;

        const char *syspath = udev_list_entry_get_name(entry);
        struct udev_device *dev = udev_device_new_from_syspath(udev, syspath);
        if (!dev)
            continue;

        const char *devnode = udev_device_get_devnode(dev);
        const char *devname = udev_device_get_sysname(dev);

        /* Skip loop devices, ram disks, etc. */
        if (!devname || strncmp(devname, "loop", 4) == 0 ||
            strncmp(devname, "ram", 3) == 0 ||
            strncmp(devname, "zram", 4) == 0) {
            udev_device_unref(dev);
            continue;
        }

        /* Get bus type */
        const char *id_bus = udev_device_get_property_value(dev, "ID_BUS");

        /* Only include USB devices for safety */
        /* TODO: Add option to show all removable devices */
        if (!id_bus || strcmp(id_bus, "usb") != 0) {
            udev_device_unref(dev);
            continue;
        }

        /* Get mountpoints and check for forbidden mounts */
        int mount_count;
        char **mounts = get_mountpoints(devnode, &mount_count);

        if (has_forbidden_mount(mounts)) {
            free_mountpoints(mounts);
            udev_device_unref(dev);
            continue;
        }

        device_info_t *info = &list->devices[list->count];

        info->name = strdup(devname);
        info->path = strdup(devnode);
        info->vendor = safe_strdup_trim(udev_device_get_property_value(dev, "ID_VENDOR"));
        info->model = safe_strdup_trim(udev_device_get_property_value(dev, "ID_MODEL"));
        info->serial = safe_strdup_trim(udev_device_get_property_value(dev, "ID_SERIAL_SHORT"));
        info->bus_type = safe_strdup_trim(id_bus);
        info->is_usb = true;
        info->removable = is_removable(devname);
        info->size = get_device_size(devname);
        info->mountpoints = mounts;
        info->mountpoint_count = mount_count;

        /* Get USB VID/PID */
        const char *vid_str = udev_device_get_property_value(dev, "ID_VENDOR_ID");
        const char *pid_str = udev_device_get_property_value(dev, "ID_MODEL_ID");
        if (vid_str)
            info->vid = (uint16_t)strtoul(vid_str, NULL, 16);
        if (pid_str)
            info->pid = (uint16_t)strtoul(pid_str, NULL, 16);

        list->count++;
        udev_device_unref(dev);
    }

    udev_enumerate_unref(enumerate);
    udev_unref(udev);

    return list;
}

void device_list_free(device_list_t *list)
{
    if (!list)
        return;

    for (int i = 0; i < list->count; i++) {
        device_info_t *dev = &list->devices[i];
        free(dev->name);
        free(dev->path);
        free(dev->vendor);
        free(dev->model);
        free(dev->serial);
        free(dev->bus_type);
        free_mountpoints(dev->mountpoints);
    }

    free(list->devices);
    free(list);
}

char *device_display_name(const device_info_t *dev)
{
    if (!dev)
        return NULL;

    char *size_str = format_size(dev->size);
    char *result = malloc(256);

    if (dev->vendor && dev->model) {
        snprintf(result, 256, "%s %s (%s)", dev->vendor, dev->model, size_str);
    } else if (dev->model) {
        snprintf(result, 256, "%s (%s)", dev->model, size_str);
    } else if (dev->vendor) {
        snprintf(result, 256, "%s (%s)", dev->vendor, size_str);
    } else {
        snprintf(result, 256, "%s (%s)", dev->path, size_str);
    }

    free(size_str);
    return result;
}

bool device_is_mounted(const device_info_t *dev)
{
    return dev && dev->mountpoint_count > 0;
}

bool device_unmount(const device_info_t *dev)
{
    if (!dev || !dev->mountpoints)
        return true;

    bool all_ok = true;
    for (int i = 0; dev->mountpoints[i]; i++) {
        if (umount2(dev->mountpoints[i], MNT_DETACH) != 0) {
            /* Try with umount command as fallback */
            char cmd[512];
            snprintf(cmd, sizeof(cmd), "umount '%s' 2>/dev/null", dev->mountpoints[i]);
            if (system(cmd) != 0) {
                rufus_error("Failed to unmount %s", dev->mountpoints[i]);
                all_ok = false;
            }
        }
    }
    return all_ok;
}

bool device_is_system_drive(const device_info_t *dev)
{
    return dev && has_forbidden_mount(dev->mountpoints);
}

device_list_t *device_refresh(void)
{
    return device_enumerate();
}

/* Monitor thread function */
static void *monitor_thread_func(void *arg)
{
    (void)arg;

    int fd = udev_monitor_get_fd(udev_mon);

    while (monitor_running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);

        struct timeval tv = {.tv_sec = 1, .tv_usec = 0};

        int ret = select(fd + 1, &fds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(fd, &fds)) {
            struct udev_device *dev = udev_monitor_receive_device(udev_mon);
            if (dev) {
                const char *action = udev_device_get_action(dev);
                if (action && (strcmp(action, "add") == 0 || strcmp(action, "remove") == 0)) {
                    if (change_callback)
                        change_callback(callback_user_data);
                }
                udev_device_unref(dev);
            }
        }
    }

    return NULL;
}

bool device_monitor_start(device_change_callback_t callback, void *user_data)
{
    if (monitor_running)
        return true;

    struct udev *udev = udev_new();
    if (!udev)
        return false;

    udev_mon = udev_monitor_new_from_netlink(udev, "udev");
    if (!udev_mon) {
        udev_unref(udev);
        return false;
    }

    udev_monitor_filter_add_match_subsystem_devtype(udev_mon, "block", "disk");
    udev_monitor_enable_receiving(udev_mon);

    change_callback = callback;
    callback_user_data = user_data;
    monitor_running = true;

    if (pthread_create(&monitor_thread, NULL, monitor_thread_func, NULL) != 0) {
        monitor_running = false;
        udev_monitor_unref(udev_mon);
        udev_mon = NULL;
        return false;
    }

    return true;
}

void device_monitor_stop(void)
{
    if (!monitor_running)
        return;

    monitor_running = false;
    pthread_join(monitor_thread, NULL);

    if (udev_mon) {
        struct udev *udev = udev_monitor_get_udev(udev_mon);
        udev_monitor_unref(udev_mon);
        udev_unref(udev);
        udev_mon = NULL;
    }

    change_callback = NULL;
    callback_user_data = NULL;
}
