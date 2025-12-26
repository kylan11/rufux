/*
 * Rufux - Hash Verification
 * Francesco Lauritano
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Compute MD5, SHA-1, SHA-256, SHA-512 hashes
 */

#ifndef RUFUS_HASH_H
#define RUFUS_HASH_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Hash types */
typedef enum {
    HASH_MD5 = 0,
    HASH_SHA1,
    HASH_SHA256,
    HASH_SHA512,
    HASH_TYPE_COUNT
} hash_type_t;

/* Hash digest sizes */
#define MD5_DIGEST_SIZE     16
#define SHA1_DIGEST_SIZE    20
#define SHA256_DIGEST_SIZE  32
#define SHA512_DIGEST_SIZE  64
#define MAX_DIGEST_SIZE     SHA512_DIGEST_SIZE

/* Progress callback for file hashing */
typedef void (*hash_progress_callback_t)(
    uint64_t bytes_processed,
    uint64_t total_bytes,
    void *user_data
);

/* Get human-readable name for hash type */
const char *hash_type_name(hash_type_t type);

/* Get digest size for hash type */
size_t hash_digest_size(hash_type_t type);

/* Hash a buffer in memory */
bool hash_buffer(hash_type_t type, const void *data, size_t len,
                 uint8_t *digest, size_t digest_len);

/* Hash a file with optional progress callback */
bool hash_file(hash_type_t type, const char *path,
               uint8_t *digest, size_t digest_len,
               hash_progress_callback_t progress_cb, void *user_data);

/* Convert digest to hex string (caller provides buffer of digest_len*2+1) */
void hash_digest_to_hex(const uint8_t *digest, size_t digest_len, char *hex);

/* Compare digest to expected hex string (case insensitive) */
bool hash_verify_hex(const uint8_t *digest, size_t digest_len, const char *expected_hex);

/* Convenience: hash file and return hex string (caller frees) */
char *hash_file_hex(hash_type_t type, const char *path,
                    hash_progress_callback_t progress_cb, void *user_data);

#endif /* RUFUS_HASH_H */
