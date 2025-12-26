/*
 * Rufux - ISO Extraction
 * Francesco Lauritano
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Extract ISO contents to a mounted partition using external tools.
 */

#ifndef RUFUS_ISO_EXTRACT_H
#define RUFUS_ISO_EXTRACT_H

#include <stdbool.h>

typedef void (*iso_extract_progress_t)(double fraction, const char *message, void *user_data);

/* Return the extraction tool name (xorriso, bsdtar, 7z) or NULL if unsupported */
const char *iso_extract_tool_name(void);

/* Check if ISO extraction is available */
bool iso_extract_is_supported(void);

/* Extract ISO contents to a mounted partition */
bool iso_extract_to_partition(const char *iso_path, const char *partition_path,
                              iso_extract_progress_t progress, void *user_data);

#endif /* RUFUS_ISO_EXTRACT_H */
