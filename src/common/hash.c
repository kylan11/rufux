/*
 * Rufux - Hash Verification Implementation
 * Francesco Lauritano
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Uses OpenSSL/libcrypto for hash computation
 */

#define _GNU_SOURCE
#include "hash.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <openssl/evp.h>

#define HASH_BUFFER_SIZE (1024 * 1024)  /* 1MB read buffer */

static const char *hash_names[] = {
    [HASH_MD5]    = "MD5",
    [HASH_SHA1]   = "SHA-1",
    [HASH_SHA256] = "SHA-256",
    [HASH_SHA512] = "SHA-512",
};

static const size_t hash_sizes[] = {
    [HASH_MD5]    = MD5_DIGEST_SIZE,
    [HASH_SHA1]   = SHA1_DIGEST_SIZE,
    [HASH_SHA256] = SHA256_DIGEST_SIZE,
    [HASH_SHA512] = SHA512_DIGEST_SIZE,
};

const char *hash_type_name(hash_type_t type)
{
    if (type >= HASH_TYPE_COUNT)
        return "Unknown";
    return hash_names[type];
}

size_t hash_digest_size(hash_type_t type)
{
    if (type >= HASH_TYPE_COUNT)
        return 0;
    return hash_sizes[type];
}

static const EVP_MD *get_evp_md(hash_type_t type)
{
    switch (type) {
    case HASH_MD5:    return EVP_md5();
    case HASH_SHA1:   return EVP_sha1();
    case HASH_SHA256: return EVP_sha256();
    case HASH_SHA512: return EVP_sha512();
    default:          return NULL;
    }
}

bool hash_buffer(hash_type_t type, const void *data, size_t len,
                 uint8_t *digest, size_t digest_len)
{
    const EVP_MD *md = get_evp_md(type);
    if (!md)
        return false;

    size_t expected_size = hash_digest_size(type);
    if (digest_len < expected_size)
        return false;

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx)
        return false;

    bool success = false;
    unsigned int actual_len = 0;

    if (EVP_DigestInit_ex(ctx, md, NULL) != 1)
        goto cleanup;

    if (EVP_DigestUpdate(ctx, data, len) != 1)
        goto cleanup;

    if (EVP_DigestFinal_ex(ctx, digest, &actual_len) != 1)
        goto cleanup;

    success = (actual_len == expected_size);

cleanup:
    EVP_MD_CTX_free(ctx);
    return success;
}

bool hash_file(hash_type_t type, const char *path,
               uint8_t *digest, size_t digest_len,
               hash_progress_callback_t progress_cb, void *user_data)
{
    const EVP_MD *md = get_evp_md(type);
    if (!md)
        return false;

    size_t expected_size = hash_digest_size(type);
    if (digest_len < expected_size)
        return false;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        rufus_error("Failed to open file for hashing: %s", path);
        return false;
    }

    /* Get file size for progress */
    fseek(fp, 0, SEEK_END);
    uint64_t total_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
        fclose(fp);
        return false;
    }

    bool success = false;
    uint8_t *buffer = malloc(HASH_BUFFER_SIZE);
    if (!buffer) {
        EVP_MD_CTX_free(ctx);
        fclose(fp);
        return false;
    }

    if (EVP_DigestInit_ex(ctx, md, NULL) != 1)
        goto cleanup;

    uint64_t bytes_read = 0;
    size_t n;

    while ((n = fread(buffer, 1, HASH_BUFFER_SIZE, fp)) > 0) {
        if (EVP_DigestUpdate(ctx, buffer, n) != 1)
            goto cleanup;

        bytes_read += n;

        if (progress_cb)
            progress_cb(bytes_read, total_size, user_data);
    }

    if (ferror(fp)) {
        rufus_error("Error reading file during hashing");
        goto cleanup;
    }

    unsigned int actual_len = 0;
    if (EVP_DigestFinal_ex(ctx, digest, &actual_len) != 1)
        goto cleanup;

    success = (actual_len == expected_size);

cleanup:
    free(buffer);
    EVP_MD_CTX_free(ctx);
    fclose(fp);
    return success;
}

void hash_digest_to_hex(const uint8_t *digest, size_t digest_len, char *hex)
{
    static const char hex_chars[] = "0123456789abcdef";

    for (size_t i = 0; i < digest_len; i++) {
        hex[i * 2]     = hex_chars[(digest[i] >> 4) & 0x0f];
        hex[i * 2 + 1] = hex_chars[digest[i] & 0x0f];
    }
    hex[digest_len * 2] = '\0';
}

bool hash_verify_hex(const uint8_t *digest, size_t digest_len, const char *expected_hex)
{
    if (!expected_hex || strlen(expected_hex) != digest_len * 2)
        return false;

    for (size_t i = 0; i < digest_len; i++) {
        char hi = tolower(expected_hex[i * 2]);
        char lo = tolower(expected_hex[i * 2 + 1]);

        int hi_val = (hi >= 'a') ? (hi - 'a' + 10) : (hi - '0');
        int lo_val = (lo >= 'a') ? (lo - 'a' + 10) : (lo - '0');

        if (digest[i] != ((hi_val << 4) | lo_val))
            return false;
    }

    return true;
}

char *hash_file_hex(hash_type_t type, const char *path,
                    hash_progress_callback_t progress_cb, void *user_data)
{
    size_t digest_size = hash_digest_size(type);
    if (digest_size == 0)
        return NULL;

    uint8_t *digest = malloc(digest_size);
    if (!digest)
        return NULL;

    if (!hash_file(type, path, digest, digest_size, progress_cb, user_data)) {
        free(digest);
        return NULL;
    }

    char *hex = malloc(digest_size * 2 + 1);
    if (!hex) {
        free(digest);
        return NULL;
    }

    hash_digest_to_hex(digest, digest_size, hex);
    free(digest);

    return hex;
}
