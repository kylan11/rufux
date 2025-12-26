/*
 * Rufux - ISO Analysis Implementation
 * Francesco Lauritano
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Uses external tools (isoinfo/file/xorriso/bsdtar/7z) for analysis
 */

#define _GNU_SOURCE
#include "iso_analyzer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>

/* Run a command and capture stdout */
static char *run_command(const char *cmd)
{
    FILE *fp = popen(cmd, "r");
    if (!fp)
        return NULL;

    char *output = NULL;
    size_t output_len = 0;
    char buf[4096];

    while (fgets(buf, sizeof(buf), fp)) {
        size_t len = strlen(buf);
        char *new_output = realloc(output, output_len + len + 1);
        if (!new_output) {
            free(output);
            pclose(fp);
            return NULL;
        }
        output = new_output;
        memcpy(output + output_len, buf, len + 1);
        output_len += len;
    }

    pclose(fp);
    return output;
}

/* Check if a command exists */
static bool command_exists(const char *cmd)
{
    char check[256];
    snprintf(check, sizeof(check), "which %s >/dev/null 2>&1", cmd);
    return system(check) == 0;
}

static char *shell_quote(const char *str)
{
    if (!str)
        return NULL;

    size_t len = 2;
    for (const char *p = str; *p; p++) {
        if (*p == '\'')
            len += 4;
        else
            len += 1;
    }

    char *out = malloc(len + 1);
    if (!out)
        return NULL;

    char *w = out;
    *w++ = '\'';
    for (const char *p = str; *p; p++) {
        if (*p == '\'') {
            *w++ = '\'';
            *w++ = '\\';
            *w++ = '\'';
            *w++ = '\'';
        } else {
            *w++ = *p;
        }
    }
    *w++ = '\'';
    *w = '\0';

    return out;
}

/* Check for El Torito boot catalog using isoinfo */
static bool has_eltorito(const char *path)
{
    if (!command_exists("isoinfo"))
        return false;

    char cmd[1024];
    char *qpath = shell_quote(path);
    if (!qpath)
        return false;
    snprintf(cmd, sizeof(cmd), "isoinfo -d -i %s 2>/dev/null", qpath);
    free(qpath);

    char *output = run_command(cmd);
    if (!output)
        return false;

    bool result = strstr(output, "El Torito") != NULL ||
                  strstr(output, "Eltorito") != NULL ||
                  strstr(output, "Boot") != NULL;
    free(output);
    return result;
}

/* Check for EFI directory using isoinfo */
static bool has_efi_dir(const char *path)
{
    char *qpath = shell_quote(path);
    if (!qpath)
        return false;

    char cmd[1024];
    if (command_exists("isoinfo")) {
        snprintf(cmd, sizeof(cmd), "isoinfo -J -f -i %s 2>/dev/null | grep -qi '/efi'", qpath);
    } else if (command_exists("xorriso")) {
        snprintf(cmd, sizeof(cmd),
                 "xorriso -indev %s -find /EFI/BOOT -print -quit 2>/dev/null | "
                 "grep -qi '/EFI/BOOT'",
                 qpath);
    } else if (command_exists("bsdtar")) {
        snprintf(cmd, sizeof(cmd),
                 "bsdtar -tf %s 2>/dev/null | grep -qi '^EFI/BOOT/'",
                 qpath);
    } else if (command_exists("7z")) {
        snprintf(cmd, sizeof(cmd),
                 "7z l -ba %s 2>/dev/null | grep -qi 'EFI/BOOT/'",
                 qpath);
    } else {
        free(qpath);
        return false;
    }

    int rc = system(cmd);
    free(qpath);
    return rc == 0;
}

/* Check if ISO is hybrid using file command */
static bool is_hybrid_iso(const char *path)
{
    if (!command_exists("file"))
        return false;

    char cmd[1024];
    char *qpath = shell_quote(path);
    if (!qpath)
        return false;
    snprintf(cmd, sizeof(cmd), "file -b %s 2>/dev/null", qpath);
    free(qpath);

    char *output = run_command(cmd);
    if (!output)
        return false;

    /* Convert to lowercase for comparison */
    for (char *p = output; *p; p++)
        *p = tolower(*p);

    bool result = strstr(output, "hybrid") != NULL ||
                  strstr(output, "bootable") != NULL;
    free(output);
    return result;
}

/* Get volume label using isoinfo */
static char *get_volume_label(const char *path)
{
    if (!command_exists("isoinfo"))
        return NULL;

    char cmd[1024];
    char *qpath = shell_quote(path);
    if (!qpath)
        return NULL;
    snprintf(cmd, sizeof(cmd), "isoinfo -d -i %s 2>/dev/null | grep 'Volume id:'", qpath);
    free(qpath);

    char *output = run_command(cmd);
    if (!output)
        return NULL;

    /* Parse "Volume id: LABEL" */
    char *colon = strchr(output, ':');
    if (colon) {
        colon++;
        while (*colon == ' ')
            colon++;

        /* Trim trailing whitespace */
        char *end = colon + strlen(colon) - 1;
        while (end > colon && isspace(*end))
            *end-- = '\0';

        char *label = strdup(colon);
        free(output);
        return label;
    }

    free(output);
    return NULL;
}

/* Check for Windows indicators */
static bool detect_windows(const char *path)
{
    if (!command_exists("isoinfo"))
        return false;

    char cmd[1024];
    char *qpath = shell_quote(path);
    if (!qpath)
        return false;
    snprintf(cmd, sizeof(cmd),
             "isoinfo -J -f -i %s 2>/dev/null | grep -qiE '(bootmgr|sources/install\\.(wim|esd))'",
             qpath);
    free(qpath);

    return system(cmd) == 0;
}

/* Check for Linux indicators */
static bool detect_linux(const char *path)
{
    if (!command_exists("isoinfo"))
        return false;

    char cmd[1024];
    char *qpath = shell_quote(path);
    if (!qpath)
        return false;
    snprintf(cmd, sizeof(cmd),
             "isoinfo -J -f -i %s 2>/dev/null | grep -qiE '(casper|isolinux|vmlinuz|initrd)'",
             qpath);
    free(qpath);

    return system(cmd) == 0;
}

iso_info_t *iso_analyze(const char *path)
{
    if (!path)
        return NULL;

    /* Check file exists and get size */
    struct stat st;
    if (stat(path, &st) != 0)
        return NULL;

    iso_info_t *info = calloc(1, sizeof(iso_info_t));
    if (!info)
        return NULL;

    info->path = strdup(path);
    info->size = st.st_size;

    /* Get volume label */
    info->label = get_volume_label(path);

    /* Detect boot capabilities */
    info->has_eltorito = has_eltorito(path);
    info->has_efi = has_efi_dir(path);
    info->is_hybrid = is_hybrid_iso(path);

    /* Determine boot type */
    if (info->has_eltorito && info->has_efi) {
        info->boot_type = BOOT_TYPE_HYBRID;
    } else if (info->has_efi) {
        info->boot_type = BOOT_TYPE_UEFI;
    } else if (info->has_eltorito) {
        info->boot_type = BOOT_TYPE_BIOS;
    } else if (info->is_hybrid) {
        info->boot_type = BOOT_TYPE_HYBRID;
    } else {
        info->boot_type = BOOT_TYPE_UNKNOWN;
    }

    info->is_bootable = (info->boot_type != BOOT_TYPE_UNKNOWN);

    /* Detect OS type */
    info->is_windows = detect_windows(path);
    info->is_linux = detect_linux(path);

    return info;
}

void iso_info_free(iso_info_t *info)
{
    if (!info)
        return;
    free(info->path);
    free(info->label);
    free(info);
}

const char *iso_boot_type_name(iso_boot_type_t type)
{
    switch (type) {
    case BOOT_TYPE_BIOS:
        return "BIOS";
    case BOOT_TYPE_UEFI:
        return "UEFI";
    case BOOT_TYPE_HYBRID:
        return "BIOS+UEFI";
    default:
        return "Unknown";
    }
}

bool iso_is_bootable(const char *path)
{
    iso_info_t *info = iso_analyze(path);
    if (!info)
        return false;

    bool result = info->is_bootable;
    iso_info_free(info);
    return result;
}

char *iso_get_label(const char *path)
{
    return get_volume_label(path);
}

uint64_t iso_get_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    return st.st_size;
}
