/*
 * Rufux - Raw Disk I/O
 * Francesco Lauritano
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef RUFUS_DISK_IO_H
#define RUFUS_DISK_IO_H

#include "../platform/platform.h"
#include <stdint.h>
#include <stdbool.h>

/* Open a device for reading/writing */
int disk_open(const char *device, bool write_access);

/* Close a device */
void disk_close(int fd);

/* Read sectors from device */
bool disk_read(int fd, uint64_t offset, void *buffer, size_t size);

/* Write sectors to device */
bool disk_write(int fd, uint64_t offset, const void *buffer, size_t size);

/* Sync device (flush writes) */
bool disk_sync(int fd);

/* Get device size in bytes */
uint64_t disk_get_size(int fd);

/* Get sector size */
uint32_t disk_get_sector_size(int fd);

/* Lock device for exclusive access */
bool disk_lock(int fd);

/* Unlock device */
bool disk_unlock(int fd);

/* Force kernel to re-read partition table */
bool disk_reread_partitions(int fd);

#endif /* RUFUS_DISK_IO_H */
