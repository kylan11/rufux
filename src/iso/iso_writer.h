/*
 * Rufux - ISO Writer
 * Francesco Lauritano
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Write ISO images to USB devices using dd
 */

#ifndef RUFUS_ISO_WRITER_H
#define RUFUS_ISO_WRITER_H

#include "../platform/platform.h"
#include <stdbool.h>
#include <stdint.h>

/* Write operation state */
typedef enum {
    WRITE_STATE_IDLE = 0,
    WRITE_STATE_WRITING,
    WRITE_STATE_SYNCING,
    WRITE_STATE_COMPLETE,
    WRITE_STATE_ERROR,
    WRITE_STATE_CANCELLED,
} write_state_t;

/* Progress callback - called periodically during write */
typedef void (*write_progress_callback_t)(
    uint64_t bytes_written,
    uint64_t total_bytes,
    double speed_mbps,
    void *user_data
);

/* Completion callback - called when write finishes */
typedef void (*write_complete_callback_t)(
    write_state_t state,
    const char *message,
    void *user_data
);

/* Writer handle */
typedef struct iso_writer iso_writer_t;

/* Create a new writer */
iso_writer_t *iso_writer_new(void);

/* Free a writer */
void iso_writer_free(iso_writer_t *writer);

/* Start writing ISO to device (async)
 * iso_path: path to ISO file
 * device_path: target device (e.g., /dev/sda)
 * progress_cb: called with progress updates
 * complete_cb: called when done
 * user_data: passed to callbacks
 */
bool iso_writer_start(iso_writer_t *writer,
                      const char *iso_path,
                      const char *device_path,
                      write_progress_callback_t progress_cb,
                      write_complete_callback_t complete_cb,
                      void *user_data);

/* Cancel an ongoing write */
void iso_writer_cancel(iso_writer_t *writer);

/* Check if write is in progress */
bool iso_writer_is_running(iso_writer_t *writer);

/* Get current state */
write_state_t iso_writer_get_state(iso_writer_t *writer);

/* Synchronous write (blocking) */
bool iso_write_sync(const char *iso_path, const char *device_path,
                    write_progress_callback_t progress_cb, void *user_data);

#endif /* RUFUS_ISO_WRITER_H */
