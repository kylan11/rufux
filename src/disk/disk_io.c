/*
 * Rufux - Raw Disk I/O Implementation
 * Francesco Lauritano
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _GNU_SOURCE
#include "disk_io.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <linux/fs.h>
#include <errno.h>
#include <string.h>

int disk_open(const char *device, bool write_access)
{
    int flags = write_access ? (O_RDWR | O_SYNC) : O_RDONLY;

    /* Try with O_DIRECT for better performance on large writes */
    int fd = open(device, flags | O_DIRECT);
    if (fd < 0) {
        /* Fallback without O_DIRECT */
        fd = open(device, flags);
    }

    if (fd < 0) {
        rufus_error("Failed to open %s: %s", device, strerror(errno));
        return -1;
    }

    return fd;
}

void disk_close(int fd)
{
    if (fd >= 0)
        close(fd);
}

bool disk_read(int fd, uint64_t offset, void *buffer, size_t size)
{
    if (lseek(fd, offset, SEEK_SET) < 0) {
        rufus_error("Failed to seek: %s", strerror(errno));
        return false;
    }

    ssize_t bytes_read = 0;
    size_t remaining = size;
    char *buf = buffer;

    while (remaining > 0) {
        ssize_t r = read(fd, buf + bytes_read, remaining);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            rufus_error("Failed to read: %s", strerror(errno));
            return false;
        }
        if (r == 0)
            break; /* EOF */
        bytes_read += r;
        remaining -= r;
    }

    return (size_t)bytes_read == size;
}

bool disk_write(int fd, uint64_t offset, const void *buffer, size_t size)
{
    if (lseek(fd, offset, SEEK_SET) < 0) {
        rufus_error("Failed to seek: %s", strerror(errno));
        return false;
    }

    ssize_t bytes_written = 0;
    size_t remaining = size;
    const char *buf = buffer;

    while (remaining > 0) {
        ssize_t w = write(fd, buf + bytes_written, remaining);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            rufus_error("Failed to write: %s", strerror(errno));
            return false;
        }
        bytes_written += w;
        remaining -= w;
    }

    return (size_t)bytes_written == size;
}

bool disk_sync(int fd)
{
    if (fsync(fd) < 0) {
        rufus_error("Failed to sync: %s", strerror(errno));
        return false;
    }
    return true;
}

uint64_t disk_get_size(int fd)
{
    uint64_t size = 0;
    if (ioctl(fd, BLKGETSIZE64, &size) < 0) {
        rufus_error("Failed to get device size: %s", strerror(errno));
        return 0;
    }
    return size;
}

uint32_t disk_get_sector_size(int fd)
{
    int sector_size = 0;
    if (ioctl(fd, BLKSSZGET, &sector_size) < 0) {
        /* Default to 512 if we can't determine */
        return 512;
    }
    return (uint32_t)sector_size;
}

bool disk_lock(int fd)
{
    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        rufus_error("Failed to lock device: %s", strerror(errno));
        return false;
    }
    return true;
}

bool disk_unlock(int fd)
{
    if (flock(fd, LOCK_UN) < 0) {
        rufus_error("Failed to unlock device: %s", strerror(errno));
        return false;
    }
    return true;
}

bool disk_reread_partitions(int fd)
{
    /* BLKRRPART tells the kernel to re-read the partition table */
    if (ioctl(fd, BLKRRPART) < 0) {
        /* This can fail if partitions are mounted, which is expected */
        if (errno != EBUSY) {
            rufus_error("Failed to re-read partition table: %s", strerror(errno));
            return false;
        }
    }
    return true;
}
