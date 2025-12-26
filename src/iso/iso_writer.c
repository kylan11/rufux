/*
 * Rufux - ISO Writer Implementation
 * Francesco Lauritano
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Uses dd with device polling for progress
 */

#define _GNU_SOURCE
#include "iso_writer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>

#define DD_BLOCK_SIZE "4M"
#define PROGRESS_POLL_MS 250

struct iso_writer {
    pthread_t thread;
    pthread_mutex_t mutex;

    /* Operation parameters */
    char *iso_path;
    char *device_path;
    uint64_t iso_size;

    /* Callbacks */
    write_progress_callback_t progress_cb;
    write_complete_callback_t complete_cb;
    void *user_data;

    /* State */
    write_state_t state;
    pid_t dd_pid;
    bool cancel_requested;
    bool thread_running;
};

iso_writer_t *iso_writer_new(void)
{
    iso_writer_t *writer = calloc(1, sizeof(iso_writer_t));
    if (!writer)
        return NULL;

    pthread_mutex_init(&writer->mutex, NULL);
    writer->state = WRITE_STATE_IDLE;
    writer->dd_pid = -1;

    return writer;
}

void iso_writer_free(iso_writer_t *writer)
{
    if (!writer)
        return;

    if (writer->thread_running) {
        iso_writer_cancel(writer);
        pthread_join(writer->thread, NULL);
    }

    pthread_mutex_destroy(&writer->mutex);
    free(writer->iso_path);
    free(writer->device_path);
    free(writer);
}

/* Read sectors written from /sys/block/DEV/stat */
static uint64_t get_device_sectors_written(const char *device_path)
{
    const char *devname = strrchr(device_path, '/');
    if (!devname)
        return 0;
    devname++; /* skip '/' */

    char statpath[256];
    snprintf(statpath, sizeof(statpath), "/sys/block/%s/stat", devname);

    FILE *fp = fopen(statpath, "r");
    if (!fp)
        return 0;

    /* /sys/block/sdX/stat format:
     * read_ios read_merges read_sectors read_ticks
     * write_ios write_merges write_sectors write_ticks
     * ... */
    unsigned long rd_ios, rd_merges, rd_sectors, rd_ticks;
    unsigned long wr_ios, wr_merges, wr_sectors, wr_ticks;

    uint64_t sectors = 0;
    if (fscanf(fp, "%lu %lu %lu %lu %lu %lu %lu %lu",
               &rd_ios, &rd_merges, &rd_sectors, &rd_ticks,
               &wr_ios, &wr_merges, &wr_sectors, &wr_ticks) >= 7) {
        sectors = wr_sectors;
    }
    fclose(fp);

    return sectors;
}

static void *writer_thread(void *arg)
{
    iso_writer_t *writer = arg;

    pthread_mutex_lock(&writer->mutex);
    writer->state = WRITE_STATE_WRITING;
    pthread_mutex_unlock(&writer->mutex);

    /* Capture baseline sectors BEFORE starting dd */
    uint64_t baseline_sectors = get_device_sectors_written(writer->device_path);
    rufus_log("Baseline sectors written: %lu", (unsigned long)baseline_sectors);

    /* Create pipe for dd output (we don't parse it, but need to drain it) */
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        rufus_error("Failed to create pipe: %s", strerror(errno));
        goto error;
    }

    pid_t pid = fork();
    if (pid == -1) {
        rufus_error("Failed to fork: %s", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        goto error;
    }

    if (pid == 0) {
        /* Child: exec pkexec dd */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        char dd_cmd[1024];
        snprintf(dd_cmd, sizeof(dd_cmd),
                 "dd bs=%s if=\"%s\" of=\"%s\" conv=fsync 2>&1",
                 DD_BLOCK_SIZE, writer->iso_path, writer->device_path);

        execlp("pkexec", "pkexec", "sh", "-c", dd_cmd, NULL);
        _exit(127);
    }

    /* Parent */
    close(pipefd[1]);

    /* Make pipe non-blocking so we can drain without stalling */
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    pthread_mutex_lock(&writer->mutex);
    writer->dd_pid = pid;
    pthread_mutex_unlock(&writer->mutex);

    rufus_log("Started dd with PID %d", pid);

    struct timespec start_time, last_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    last_time = start_time;
    uint64_t last_bytes = 0;

    while (1) {
        /* Check for cancellation */
        pthread_mutex_lock(&writer->mutex);
        bool cancelled = writer->cancel_requested;
        pthread_mutex_unlock(&writer->mutex);

        if (cancelled) {
            kill(pid, SIGTERM);
            break;
        }

        /* Drain pipe to prevent dd from blocking */
        char drain[1024];
        while (read(pipefd[0], drain, sizeof(drain)) > 0)
            ;

        /* Check if dd finished */
        int wstatus;
        pid_t ret = waitpid(pid, &wstatus, WNOHANG);
        if (ret == pid) {
            close(pipefd[0]);

            if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0) {
                goto success;
            } else {
                rufus_error("dd exited with status %d", WEXITSTATUS(wstatus));
                goto error;
            }
        } else if (ret == -1 && errno != EINTR) {
            rufus_error("waitpid failed: %s", strerror(errno));
            close(pipefd[0]);
            goto error;
        }

        /* Get current sectors and subtract baseline */
        uint64_t current_sectors = get_device_sectors_written(writer->device_path);
        uint64_t delta_sectors = current_sectors - baseline_sectors;
        uint64_t bytes_written = delta_sectors * 512ULL;

        /* Cap at iso_size */
        if (bytes_written > writer->iso_size)
            bytes_written = writer->iso_size;

        /* Calculate speed */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - last_time.tv_sec) +
                         (now.tv_nsec - last_time.tv_nsec) / 1e9;

        double speed = 0;
        if (elapsed >= 0.25 && bytes_written > last_bytes) {
            speed = (double)(bytes_written - last_bytes) / elapsed / (1024.0 * 1024.0);
            last_bytes = bytes_written;
            last_time = now;
        }

        if (writer->progress_cb)
            writer->progress_cb(bytes_written, writer->iso_size, speed, writer->user_data);

        usleep(PROGRESS_POLL_MS * 1000);
    }

    /* Cancelled path */
    close(pipefd[0]);
    waitpid(pid, NULL, 0);

    pthread_mutex_lock(&writer->mutex);
    writer->state = WRITE_STATE_CANCELLED;
    writer->dd_pid = -1;
    pthread_mutex_unlock(&writer->mutex);

    if (writer->complete_cb)
        writer->complete_cb(WRITE_STATE_CANCELLED, "Write cancelled", writer->user_data);

    writer->thread_running = false;
    return NULL;

success:
    pthread_mutex_lock(&writer->mutex);
    writer->state = WRITE_STATE_SYNCING;
    writer->dd_pid = -1;
    pthread_mutex_unlock(&writer->mutex);

    sync();

    pthread_mutex_lock(&writer->mutex);
    writer->state = WRITE_STATE_COMPLETE;
    pthread_mutex_unlock(&writer->mutex);

    if (writer->progress_cb)
        writer->progress_cb(writer->iso_size, writer->iso_size, 0, writer->user_data);

    if (writer->complete_cb)
        writer->complete_cb(WRITE_STATE_COMPLETE, "Write complete", writer->user_data);

    writer->thread_running = false;
    return NULL;

error:
    pthread_mutex_lock(&writer->mutex);
    writer->state = WRITE_STATE_ERROR;
    writer->dd_pid = -1;
    pthread_mutex_unlock(&writer->mutex);

    if (writer->complete_cb)
        writer->complete_cb(WRITE_STATE_ERROR, "Write failed", writer->user_data);

    writer->thread_running = false;
    return NULL;
}

bool iso_writer_start(iso_writer_t *writer,
                      const char *iso_path,
                      const char *device_path,
                      write_progress_callback_t progress_cb,
                      write_complete_callback_t complete_cb,
                      void *user_data)
{
    if (!writer || !iso_path || !device_path)
        return false;

    pthread_mutex_lock(&writer->mutex);
    if (writer->thread_running) {
        pthread_mutex_unlock(&writer->mutex);
        rufus_error("Write already in progress");
        return false;
    }
    pthread_mutex_unlock(&writer->mutex);

    struct stat st;
    if (stat(iso_path, &st) != 0) {
        rufus_error("Cannot stat ISO file: %s", strerror(errno));
        return false;
    }

    free(writer->iso_path);
    free(writer->device_path);
    writer->iso_path = strdup(iso_path);
    writer->device_path = strdup(device_path);
    writer->iso_size = st.st_size;
    writer->progress_cb = progress_cb;
    writer->complete_cb = complete_cb;
    writer->user_data = user_data;
    writer->cancel_requested = false;
    writer->thread_running = true;

    if (pthread_create(&writer->thread, NULL, writer_thread, writer) != 0) {
        rufus_error("Failed to create writer thread");
        writer->thread_running = false;
        return false;
    }

    pthread_detach(writer->thread);
    return true;
}

void iso_writer_cancel(iso_writer_t *writer)
{
    if (!writer)
        return;

    pthread_mutex_lock(&writer->mutex);
    writer->cancel_requested = true;
    if (writer->dd_pid > 0) {
        kill(writer->dd_pid, SIGTERM);
    }
    pthread_mutex_unlock(&writer->mutex);
}

bool iso_writer_is_running(iso_writer_t *writer)
{
    if (!writer)
        return false;

    pthread_mutex_lock(&writer->mutex);
    bool running = writer->thread_running;
    pthread_mutex_unlock(&writer->mutex);

    return running;
}

write_state_t iso_writer_get_state(iso_writer_t *writer)
{
    if (!writer)
        return WRITE_STATE_IDLE;

    pthread_mutex_lock(&writer->mutex);
    write_state_t state = writer->state;
    pthread_mutex_unlock(&writer->mutex);

    return state;
}

bool iso_write_sync(const char *iso_path, const char *device_path,
                    write_progress_callback_t progress_cb, void *user_data)
{
    struct stat st;
    if (stat(iso_path, &st) != 0) {
        rufus_error("Cannot stat ISO file: %s", strerror(errno));
        return false;
    }

    uint64_t iso_size = st.st_size;

    /* Capture baseline before starting */
    uint64_t baseline_sectors = get_device_sectors_written(device_path);

    pid_t pid = fork();
    if (pid == -1)
        return false;

    if (pid == 0) {
        /* Redirect output to /dev/null */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        char if_arg[1024], of_arg[1024];
        snprintf(if_arg, sizeof(if_arg), "if=%s", iso_path);
        snprintf(of_arg, sizeof(of_arg), "of=%s", device_path);

        execlp("pkexec", "pkexec", "dd",
               "bs=" DD_BLOCK_SIZE,
               if_arg, of_arg,
               "conv=fsync", NULL);
        _exit(127);
    }

    struct timespec last_time;
    clock_gettime(CLOCK_MONOTONIC, &last_time);
    uint64_t last_bytes = 0;

    while (1) {
        int wstatus;
        pid_t ret = waitpid(pid, &wstatus, WNOHANG);
        if (ret == pid) {
            sync();
            if (progress_cb)
                progress_cb(iso_size, iso_size, 0, user_data);
            return WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0;
        }

        uint64_t current_sectors = get_device_sectors_written(device_path);
        uint64_t bytes_written = (current_sectors - baseline_sectors) * 512ULL;
        if (bytes_written > iso_size)
            bytes_written = iso_size;

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - last_time.tv_sec) +
                         (now.tv_nsec - last_time.tv_nsec) / 1e9;

        double speed = 0;
        if (elapsed >= 0.25 && bytes_written > last_bytes) {
            speed = (double)(bytes_written - last_bytes) / elapsed / (1024.0 * 1024.0);
            last_bytes = bytes_written;
            last_time = now;
        }

        if (progress_cb)
            progress_cb(bytes_written, iso_size, speed, user_data);

        usleep(PROGRESS_POLL_MS * 1000);
    }
}
