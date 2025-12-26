/*
 * Rufux - Main Window Implementation
 * Francesco Lauritano
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "window.h"
#include "widgets.h"
#include "../device/device.h"
#include "../disk/partition.h"
#include "../format/format.h"
#include "../iso/iso_analyzer.h"
#include "../iso/iso_extract.h"
#include "../iso/iso_writer.h"
#include "../common/hash.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

struct _RufusWindow {
    GtkApplicationWindow parent_instance;

    /* Widgets */
    GtkDropDown *device_dropdown;
    GtkButton *refresh_button;
    GtkDropDown *boot_dropdown;
    GtkEntry *iso_entry;
    GtkButton *select_button;
    GtkDropDown *write_mode_dropdown;
    GtkDropDown *partition_dropdown;
    GtkDropDown *target_dropdown;
    GtkEntry *label_entry;
    GtkDropDown *fs_dropdown;
    GtkDropDown *cluster_dropdown;
    GtkProgressBar *progress_bar;
    GtkLabel *status_label;
    GtkLabel *hash_label;
    GtkButton *start_button;
    GtkButton *close_button;

    /* State */
    device_list_t *devices;
    char *iso_path;
    iso_info_t *iso_info;
    iso_writer_t *iso_writer;
    gboolean operation_running;
    gboolean hash_in_progress;
    char *iso_hash;
};

G_DEFINE_TYPE(RufusWindow, rufus_window, GTK_TYPE_APPLICATION_WINDOW)

static const char *boot_options[] = { "Disk or ISO image", "Non bootable", NULL };
static const char *write_mode_options[] = { "DD image (raw)", "ISO file copy (UEFI only)", NULL };
static const char *fs_options[] = { "FAT32", "NTFS", "exFAT", "ext4", NULL };
static const char *partition_options[] = { "MBR", "GPT", NULL };
static const char *target_options[] = { "BIOS", "UEFI", "BIOS+UEFI", NULL };
static const char *cluster_options[] = { "Default", "4096", "8192", "16384", "32768", NULL };

static void update_start_sensitivity(RufusWindow *self);
static void set_status(RufusWindow *self, const char *text, const char *css_class);
static void reset_status_ready(RufusWindow *self);

static void refresh_devices(RufusWindow *self)
{
    if (self->devices) {
        device_list_free(self->devices);
    }
    self->devices = device_enumerate();

    GtkStringList *model = gtk_string_list_new(NULL);
    if (self->devices) {
        for (int i = 0; i < self->devices->count; i++) {
            char *name = device_display_name(&self->devices->devices[i]);
            gtk_string_list_append(model, name);
            free(name);
        }
    }

    gtk_drop_down_set_model(self->device_dropdown, G_LIST_MODEL(model));
    g_object_unref(model);

    if (self->devices && self->devices->count > 0) {
        gtk_drop_down_set_selected(self->device_dropdown, 0);
    }
}

static void set_status(RufusWindow *self, const char *text, const char *css_class)
{
    gtk_label_set_text(self->status_label, text);

    gtk_widget_remove_css_class(GTK_WIDGET(self->status_label), "status-ready");
    gtk_widget_remove_css_class(GTK_WIDGET(self->status_label), "status-busy");
    gtk_widget_remove_css_class(GTK_WIDGET(self->status_label), "status-error");

    if (css_class)
        gtk_widget_add_css_class(GTK_WIDGET(self->status_label), css_class);
}

static void reset_status_ready(RufusWindow *self)
{
    if (self->operation_running)
        return;

    if (!self->progress_bar || !self->status_label)
        return;

    gtk_progress_bar_set_fraction(self->progress_bar, 0.0);
    gtk_progress_bar_set_text(self->progress_bar, "0%");
    set_status(self, "READY", "status-ready");
}

static void on_param_changed(GObject *object, GParamSpec *pspec, RufusWindow *self)
{
    (void)object;
    (void)pspec;

    if (!self->start_button || !self->progress_bar || !self->status_label)
        return;

    reset_status_ready(self);
    update_start_sensitivity(self);
}

static void on_refresh_clicked(GtkButton *button, RufusWindow *self)
{
    (void)button;
    refresh_devices(self);
    reset_status_ready(self);
}

static void update_start_sensitivity(RufusWindow *self)
{
    gboolean has_device = gtk_drop_down_get_selected(self->device_dropdown) != GTK_INVALID_LIST_POSITION;
    guint boot_mode = gtk_drop_down_get_selected(self->boot_dropdown);

    /* If ISO mode selected, require an ISO */
    gboolean can_start;
    if (boot_mode == 0) {
        /* ISO mode - need device AND ISO */
        can_start = has_device && self->iso_path && !self->operation_running;
    } else {
        /* Non-bootable - just need device */
        can_start = has_device && !self->operation_running;
    }

    gtk_widget_set_sensitive(GTK_WIDGET(self->start_button), can_start);
}

/* ============== Hash Calculation ============== */

typedef struct {
    RufusWindow *window;
    char *path;
    char *hash;
} hash_op_t;

static gboolean hash_complete_idle(gpointer data)
{
    hash_op_t *op = data;
    RufusWindow *self = op->window;

    self->hash_in_progress = FALSE;

    if (op->hash) {
        g_free(self->iso_hash);
        self->iso_hash = g_strdup(op->hash);

        /* Show truncated hash in label */
        char display[32];
        snprintf(display, sizeof(display), "SHA-256: %.16s...", op->hash);
        gtk_label_set_text(self->hash_label, display);
        gtk_widget_set_tooltip_text(GTK_WIDGET(self->hash_label), op->hash);
    } else {
        gtk_label_set_text(self->hash_label, "SHA-256: (error)");
    }

    free(op->path);
    free(op->hash);
    g_free(op);

    return G_SOURCE_REMOVE;
}

static void *hash_thread_func(void *data)
{
    hash_op_t *op = data;

    op->hash = hash_file_hex(HASH_SHA256, op->path, NULL, NULL);

    g_idle_add(hash_complete_idle, op);
    return NULL;
}

static void start_hash_calculation(RufusWindow *self, const char *path)
{
    if (self->hash_in_progress)
        return;

    gtk_label_set_text(self->hash_label, "SHA-256: calculating...");
    gtk_widget_set_tooltip_text(GTK_WIDGET(self->hash_label), NULL);

    hash_op_t *op = g_new0(hash_op_t, 1);
    op->window = self;
    op->path = strdup(path);

    self->hash_in_progress = TRUE;

    pthread_t thread;
    pthread_create(&thread, NULL, hash_thread_func, op);
    pthread_detach(thread);
}

/* ISO file selection callback */
static void on_iso_file_selected(GObject *source, GAsyncResult *result, gpointer user_data)
{
    RufusWindow *self = RUFUS_WINDOW(user_data);
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);

    GFile *file = gtk_file_dialog_open_finish(dialog, result, NULL);
    if (file) {
        g_free(self->iso_path);
        self->iso_path = g_file_get_path(file);

        /* Update entry */
        gtk_editable_set_text(GTK_EDITABLE(self->iso_entry), self->iso_path);

        /* Analyze ISO */
        if (self->iso_info) {
            iso_info_free(self->iso_info);
        }
        self->iso_info = iso_analyze(self->iso_path);

        if (self->iso_info) {
            if (self->iso_info->label) {
                gtk_editable_set_text(GTK_EDITABLE(self->label_entry), self->iso_info->label);
            }

            if (self->iso_info->is_windows) {
                gtk_drop_down_set_selected(self->write_mode_dropdown, 0);
            } else if (!self->iso_info->is_hybrid && self->iso_info->has_efi) {
                gtk_drop_down_set_selected(self->write_mode_dropdown, 1);
            } else {
                gtk_drop_down_set_selected(self->write_mode_dropdown, 0);
            }
        }

        /* Start background hash calculation */
        start_hash_calculation(self, self->iso_path);

        g_object_unref(file);
        reset_status_ready(self);
        update_start_sensitivity(self);
    }
}

static void on_select_clicked(GtkButton *button, RufusWindow *self)
{
    (void)button;

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select ISO Image");

    /* Create filter for ISO files */
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "ISO Images (*.iso)");
    gtk_file_filter_add_pattern(filter, "*.iso");
    gtk_file_filter_add_pattern(filter, "*.ISO");
    gtk_file_filter_add_mime_type(filter, "application/x-iso9660-image");

    GtkFileFilter *all_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(all_filter, "All Files");
    gtk_file_filter_add_pattern(all_filter, "*");

    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, filter);
    g_list_store_append(filters, all_filter);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    gtk_file_dialog_set_default_filter(dialog, filter);

    gtk_file_dialog_open(dialog, GTK_WINDOW(self), NULL, on_iso_file_selected, self);

    g_object_unref(filter);
    g_object_unref(all_filter);
    g_object_unref(filters);
    g_object_unref(dialog);
}

static void on_boot_mode_changed(GtkDropDown *dropdown, GParamSpec *pspec, RufusWindow *self)
{
    (void)pspec;
    guint mode = gtk_drop_down_get_selected(dropdown);

    /* Enable/disable ISO selection based on mode */
    gboolean iso_mode = (mode == 0);
    gtk_widget_set_sensitive(GTK_WIDGET(self->iso_entry), iso_mode);
    gtk_widget_set_sensitive(GTK_WIDGET(self->select_button), iso_mode);
    gtk_widget_set_sensitive(GTK_WIDGET(self->write_mode_dropdown), iso_mode);

    reset_status_ready(self);
    update_start_sensitivity(self);
}

static void on_write_mode_changed(GtkDropDown *dropdown, GParamSpec *pspec, RufusWindow *self)
{
    (void)pspec;
    guint mode = gtk_drop_down_get_selected(dropdown);

    if (mode == 1) {
        gtk_drop_down_set_selected(self->fs_dropdown, 0);
        gtk_drop_down_set_selected(self->target_dropdown, 1);
    }

    reset_status_ready(self);
    update_start_sensitivity(self);
}

/* ============== Write Operation ============== */

typedef struct {
    RufusWindow *window;
    char *device_path;
    char *iso_path;
    char *partition_path;
    partition_style_t part_style;
    target_type_t target;
    fs_type_t fs_type;
    uint32_t cluster_size;
    char *label;
    gboolean write_iso;
    gboolean iso_extract;
    gboolean success;
} write_op_t;

static gboolean write_complete_idle(gpointer data)
{
    write_op_t *op = data;
    RufusWindow *self = op->window;

    self->operation_running = FALSE;
    gtk_widget_set_sensitive(GTK_WIDGET(self->device_dropdown), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->refresh_button), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->close_button), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->select_button), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->write_mode_dropdown),
                             gtk_drop_down_get_selected(self->boot_dropdown) == 0);
    update_start_sensitivity(self);

    if (op->success) {
        gtk_progress_bar_set_fraction(self->progress_bar, 1.0);
        gtk_progress_bar_set_text(self->progress_bar, "100%");
        set_status(self, "Completed", "status-ready");
    } else {
        set_status(self, "Operation failed", "status-error");
    }

    g_free(op->device_path);
    g_free(op->iso_path);
    g_free(op->partition_path);
    g_free(op->label);
    g_free(op);

    return G_SOURCE_REMOVE;
}

typedef struct {
    RufusWindow *window;
    double fraction;
    char text[128];
} progress_update_t;

static gboolean progress_update_idle(gpointer data)
{
    progress_update_t *update = data;

    gtk_progress_bar_set_fraction(update->window->progress_bar, update->fraction);
    gtk_progress_bar_set_text(update->window->progress_bar, update->text);

    g_free(update);
    return G_SOURCE_REMOVE;
}

static void iso_write_progress(uint64_t bytes, uint64_t total, double speed, void *user_data)
{
    write_op_t *op = user_data;

    progress_update_t *update = g_new0(progress_update_t, 1);
    update->window = op->window;
    update->fraction = total > 0 ? (double)bytes / total : 0.0;

    char *size_done = format_size(bytes);
    char *size_total = format_size(total);
    snprintf(update->text, sizeof(update->text), "%s / %s (%.1f MB/s)",
             size_done, size_total, speed);
    free(size_done);
    free(size_total);

    g_idle_add(progress_update_idle, update);
}

static void iso_extract_progress(double fraction, const char *message, void *user_data)
{
    write_op_t *op = user_data;

    progress_update_t *update = g_new0(progress_update_t, 1);
    update->window = op->window;
    update->fraction = fraction;

    if (message)
        snprintf(update->text, sizeof(update->text), "%s", message);
    else
        update->text[0] = '\0';

    g_idle_add(progress_update_idle, update);
}

static void *write_thread_func(void *data)
{
    write_op_t *op = data;

    if (op->write_iso) {
        if (op->iso_extract) {
            rufus_log("Extracting ISO %s to %s", op->iso_path, op->device_path);

            if (!partition_create_single_efi(op->device_path, op->part_style, op->label)) {
                op->success = FALSE;
                g_idle_add(write_complete_idle, op);
                return NULL;
            }

            usleep(1000000);

            op->partition_path = partition_get_path(op->device_path, 1);
            if (!op->partition_path) {
                op->success = FALSE;
                g_idle_add(write_complete_idle, op);
                return NULL;
            }

            format_options_t fmt_opts = {
                .fs_type = FS_FAT32,
                .label = op->label,
                .cluster_size = op->cluster_size,
                .quick_format = TRUE,
            };

            if (!format_partition(op->partition_path, &fmt_opts, NULL, NULL)) {
                op->success = FALSE;
                g_idle_add(write_complete_idle, op);
                return NULL;
            }

            op->success = iso_extract_to_partition(op->iso_path, op->partition_path,
                                                   iso_extract_progress, op);
        } else {
            /* ISO write mode - just dd the ISO */
            rufus_log("Writing ISO %s to %s", op->iso_path, op->device_path);

            op->success = iso_write_sync(op->iso_path, op->device_path,
                                         iso_write_progress, op);
        }
    } else {
        /* Format-only mode */
        rufus_log("Formatting %s as %s", op->device_path, fs_type_name(op->fs_type));

        bool needs_esp = (op->target != TARGET_BIOS &&
                          op->part_style == PARTITION_STYLE_GPT);

        if (needs_esp) {
            if (!partition_create_bootable(op->device_path, op->part_style,
                                           op->target, op->fs_type, op->label)) {
                op->success = FALSE;
                g_idle_add(write_complete_idle, op);
                return NULL;
            }

            usleep(1000000);

            char *esp_path = partition_get_path(op->device_path, 1);
            op->partition_path = partition_get_path(op->device_path, 2);
            if (!esp_path || !op->partition_path) {
                free(esp_path);
                op->success = FALSE;
                g_idle_add(write_complete_idle, op);
                return NULL;
            }

            format_options_t esp_opts = {
                .fs_type = FS_FAT32,
                .label = "EFI",
                .cluster_size = 0,
                .quick_format = TRUE,
            };

            if (!format_partition(esp_path, &esp_opts, NULL, NULL)) {
                free(esp_path);
                op->success = FALSE;
                g_idle_add(write_complete_idle, op);
                return NULL;
            }

            free(esp_path);
        } else {
            if (!partition_create_single(op->device_path, op->part_style,
                                         op->fs_type, op->label)) {
                op->success = FALSE;
                g_idle_add(write_complete_idle, op);
                return NULL;
            }

            usleep(1000000);

            op->partition_path = partition_get_path(op->device_path, 1);
            if (!op->partition_path) {
                op->success = FALSE;
                g_idle_add(write_complete_idle, op);
                return NULL;
            }
        }

        format_options_t fmt_opts = {
            .fs_type = op->fs_type,
            .label = op->label,
            .cluster_size = op->cluster_size,
            .quick_format = TRUE,
        };

        op->success = format_partition(op->partition_path, &fmt_opts, NULL, NULL);
    }

    g_idle_add(write_complete_idle, op);
    return NULL;
}

/* Confirmation dialog callback */
static void on_confirm_response(GObject *source, GAsyncResult *result, gpointer user_data)
{
    write_op_t *op = user_data;
    GtkAlertDialog *dialog = GTK_ALERT_DIALOG(source);

    int response = gtk_alert_dialog_choose_finish(dialog, result, NULL);

    if (response == 1) {
        /* User confirmed - start operation */
        RufusWindow *self = op->window;

        self->operation_running = TRUE;
        gtk_widget_set_sensitive(GTK_WIDGET(self->device_dropdown), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(self->refresh_button), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(self->start_button), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(self->close_button), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(self->select_button), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(self->write_mode_dropdown), FALSE);

        gtk_progress_bar_set_fraction(self->progress_bar, 0.0);
        gtk_progress_bar_set_text(self->progress_bar, "0%");
        if (op->write_iso && op->iso_extract) {
            set_status(self, "Extracting ISO...", "status-busy");
        } else {
            set_status(self, op->write_iso ? "Writing ISO..." : "Formatting...", "status-busy");
        }

        pthread_t thread;
        pthread_create(&thread, NULL, write_thread_func, op);
        pthread_detach(thread);
    } else {
        /* Cancelled */
        g_free(op->device_path);
        g_free(op->iso_path);
        g_free(op->label);
        g_free(op);
    }
}

static void on_start_clicked(GtkButton *button, RufusWindow *self)
{
    (void)button;

    guint device_idx = gtk_drop_down_get_selected(self->device_dropdown);
    if (device_idx == GTK_INVALID_LIST_POSITION || !self->devices ||
        device_idx >= (guint)self->devices->count) {
        set_status(self, "No device selected", "status-error");
        return;
    }

    const device_info_t *dev = &self->devices->devices[device_idx];
    guint boot_mode = gtk_drop_down_get_selected(self->boot_dropdown);
    gboolean write_iso = (boot_mode == 0 && self->iso_path != NULL);
    guint write_mode = gtk_drop_down_get_selected(self->write_mode_dropdown);
    gboolean iso_extract = (write_mode == 1);

    if (write_iso && !self->iso_info) {
        set_status(self, "Please select an ISO image", "status-error");
        return;
    }

    if (write_iso && !self->iso_info->is_bootable) {
        set_status(self, "Warning: ISO may not be bootable", "status-error");
    }

    if (write_iso && self->iso_info && self->iso_info->size > dev->size) {
        set_status(self, "ISO is larger than the target device", "status-error");
        return;
    }

    if (write_iso && iso_extract) {
        if (!iso_extract_is_supported()) {
            set_status(self, "ISO file copy needs xorriso, bsdtar, or 7z", "status-error");
            return;
        }
        if (self->iso_info->is_windows) {
            set_status(self, "Windows ISO extraction not supported yet", "status-error");
            return;
        }
        if (!self->iso_info->has_efi) {
            set_status(self, "ISO file copy requires UEFI boot files", "status-error");
            return;
        }
        if (gtk_drop_down_get_selected(self->target_dropdown) == 0) {
            set_status(self, "ISO file copy requires UEFI target", "status-error");
            return;
        }
        if (gtk_drop_down_get_selected(self->fs_dropdown) != 0) {
            set_status(self, "ISO file copy requires FAT32", "status-error");
            return;
        }
    }

    /* Unmount device first */
    if (device_is_mounted(dev)) {
        device_unmount(dev);
        usleep(500000);
    }

    /* Prepare operation */
    write_op_t *op = g_new0(write_op_t, 1);
    op->window = self;
    op->device_path = g_strdup(dev->path);
    op->write_iso = write_iso;
    op->iso_extract = write_iso && iso_extract;

    if (write_iso) {
        op->iso_path = g_strdup(self->iso_path);
        if (op->iso_extract) {
            op->part_style = gtk_drop_down_get_selected(self->partition_dropdown) == 1 ?
                             PARTITION_STYLE_GPT : PARTITION_STYLE_MBR;

            guint target_idx = gtk_drop_down_get_selected(self->target_dropdown);
            switch (target_idx) {
            case 1: op->target = TARGET_UEFI; break;
            case 2: op->target = TARGET_BIOS_UEFI; break;
            default: op->target = TARGET_BIOS; break;
            }

            op->fs_type = FS_FAT32;

            guint cluster_idx = gtk_drop_down_get_selected(self->cluster_dropdown);
            if (cluster_idx > 0 && cluster_options[cluster_idx]) {
                op->cluster_size = (uint32_t)strtoul(cluster_options[cluster_idx], NULL, 10);
            } else {
                op->cluster_size = 0;
            }

            op->label = g_strdup(gtk_editable_get_text(GTK_EDITABLE(self->label_entry)));
        }
    } else {
        op->part_style = gtk_drop_down_get_selected(self->partition_dropdown) == 1 ?
                         PARTITION_STYLE_GPT : PARTITION_STYLE_MBR;

        guint target_idx = gtk_drop_down_get_selected(self->target_dropdown);
        switch (target_idx) {
        case 1: op->target = TARGET_UEFI; break;
        case 2: op->target = TARGET_BIOS_UEFI; break;
        default: op->target = TARGET_BIOS; break;
        }

        guint fs_idx = gtk_drop_down_get_selected(self->fs_dropdown);
        switch (fs_idx) {
        case 0: op->fs_type = FS_FAT32; break;
        case 1: op->fs_type = FS_NTFS; break;
        case 2: op->fs_type = FS_EXFAT; break;
        case 3: op->fs_type = FS_EXT4; break;
        default: op->fs_type = FS_FAT32; break;
        }

        guint cluster_idx = gtk_drop_down_get_selected(self->cluster_dropdown);
        if (cluster_idx > 0 && cluster_options[cluster_idx]) {
            op->cluster_size = (uint32_t)strtoul(cluster_options[cluster_idx], NULL, 10);
        } else {
            op->cluster_size = 0;
        }

        op->label = g_strdup(gtk_editable_get_text(GTK_EDITABLE(self->label_entry)));
    }

    op->success = FALSE;

    /* Show confirmation dialog */
    char *size_str = format_size(dev->size);
    char *message;
    if (write_iso) {
        message = g_strdup_printf(
            "This will ERASE ALL DATA on %s (%s) and write:\n\n%s\n\nContinue?",
            dev->path, size_str, self->iso_path);
    } else {
        message = g_strdup_printf(
            "This will ERASE ALL DATA on %s (%s) and format it as %s.\n\nContinue?",
            dev->path, size_str, fs_type_name(op->fs_type));
    }
    free(size_str);

    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", message);
    gtk_alert_dialog_set_buttons(dialog, (const char *[]){"Cancel", "Continue", NULL});
    gtk_alert_dialog_set_cancel_button(dialog, 0);
    gtk_alert_dialog_set_default_button(dialog, 0);

    gtk_alert_dialog_choose(dialog, GTK_WINDOW(self), NULL, on_confirm_response, op);

    g_free(message);
    g_object_unref(dialog);
}

static void on_close_clicked(GtkButton *button, RufusWindow *self)
{
    (void)button;
    gtk_window_close(GTK_WINDOW(self));
}

/* ============== Device Hotplug Monitoring ============== */

static RufusWindow *hotplug_window = NULL;

static gboolean on_device_change_idle(gpointer user_data)
{
    RufusWindow *self = RUFUS_WINDOW(user_data);

    /* Don't refresh during an operation */
    if (!self->operation_running) {
        rufus_log("USB device change detected, refreshing list");
        refresh_devices(self);
        update_start_sensitivity(self);
    }

    return G_SOURCE_REMOVE;
}

static void on_device_change(void *user_data)
{
    /* Schedule refresh on main thread */
    g_idle_add(on_device_change_idle, user_data);
}

static void rufus_window_dispose(GObject *object)
{
    RufusWindow *self = RUFUS_WINDOW(object);

    /* Stop device monitoring */
    if (hotplug_window == self) {
        device_monitor_stop();
        hotplug_window = NULL;
    }

    if (self->devices) {
        device_list_free(self->devices);
        self->devices = NULL;
    }

    if (self->iso_info) {
        iso_info_free(self->iso_info);
        self->iso_info = NULL;
    }

    if (self->iso_writer) {
        iso_writer_free(self->iso_writer);
        self->iso_writer = NULL;
    }

    g_free(self->iso_path);
    self->iso_path = NULL;

    g_free(self->iso_hash);
    self->iso_hash = NULL;

    G_OBJECT_CLASS(rufus_window_parent_class)->dispose(object);
}

static void rufus_window_class_init(RufusWindowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = rufus_window_dispose;
}

static GtkWidget *create_section(const char *title, GtkWidget *content)
{
    GtkWidget *expander = gtk_expander_new(title);
    gtk_expander_set_expanded(GTK_EXPANDER(expander), TRUE);
    gtk_widget_add_css_class(expander, "section-expander");

    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(frame, "section-frame");
    gtk_frame_set_child(GTK_FRAME(frame), content);
    gtk_expander_set_child(GTK_EXPANDER(expander), frame);

    return expander;
}

static void rufus_window_init(RufusWindow *self)
{
    gtk_window_set_title(GTK_WINDOW(self), "Rufux");
    gtk_window_set_default_size(GTK_WINDOW(self), 520, 580);

    self->iso_writer = iso_writer_new();

    /* Main container */
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(main_box, 16);
    gtk_widget_set_margin_bottom(main_box, 16);
    gtk_widget_set_margin_start(main_box, 16);
    gtk_widget_set_margin_end(main_box, 16);

    /* === Drive Properties Section === */
    GtkWidget *drive_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(drive_grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(drive_grid), 8);
    gtk_widget_set_margin_top(drive_grid, 8);
    gtk_widget_set_margin_bottom(drive_grid, 8);
    gtk_widget_set_margin_start(drive_grid, 8);
    gtk_widget_set_margin_end(drive_grid, 8);

    /* Device row */
    GtkWidget *device_label = gtk_label_new("Device");
    gtk_widget_set_halign(device_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(drive_grid), device_label, 0, 0, 1, 1);

    self->device_dropdown = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(NULL));
    gtk_widget_set_hexpand(GTK_WIDGET(self->device_dropdown), TRUE);
    g_signal_connect(self->device_dropdown, "notify::selected",
                     G_CALLBACK(on_param_changed), self);

    self->refresh_button = GTK_BUTTON(gtk_button_new_from_icon_name("view-refresh-symbolic"));
    gtk_widget_set_tooltip_text(GTK_WIDGET(self->refresh_button), "Refresh device list");
    g_signal_connect(self->refresh_button, "clicked", G_CALLBACK(on_refresh_clicked), self);

    GtkWidget *device_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(device_box), GTK_WIDGET(self->device_dropdown));
    gtk_box_append(GTK_BOX(device_box), GTK_WIDGET(self->refresh_button));
    gtk_widget_set_hexpand(device_box, TRUE);
    gtk_grid_attach(GTK_GRID(drive_grid), device_box, 1, 0, 3, 1);

    /* Boot selection row */
    GtkWidget *boot_label = gtk_label_new("Boot selection");
    gtk_widget_set_halign(boot_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(drive_grid), boot_label, 0, 1, 1, 1);

    self->boot_dropdown = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(boot_options));
    gtk_widget_set_hexpand(GTK_WIDGET(self->boot_dropdown), TRUE);
    g_signal_connect(self->boot_dropdown, "notify::selected", G_CALLBACK(on_boot_mode_changed), self);
    gtk_grid_attach(GTK_GRID(drive_grid), GTK_WIDGET(self->boot_dropdown), 1, 1, 3, 1);

    /* ISO selection row */
    GtkWidget *iso_label = gtk_label_new("ISO image");
    gtk_widget_set_halign(iso_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(drive_grid), iso_label, 0, 2, 1, 1);

    self->iso_entry = GTK_ENTRY(gtk_entry_new());
    gtk_editable_set_editable(GTK_EDITABLE(self->iso_entry), FALSE);
    gtk_entry_set_placeholder_text(self->iso_entry, "Click SELECT to choose an ISO...");
    gtk_widget_set_hexpand(GTK_WIDGET(self->iso_entry), TRUE);

    self->select_button = GTK_BUTTON(gtk_button_new_with_label("SELECT"));
    g_signal_connect(self->select_button, "clicked", G_CALLBACK(on_select_clicked), self);

    GtkWidget *iso_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(iso_box), GTK_WIDGET(self->iso_entry));
    gtk_box_append(GTK_BOX(iso_box), GTK_WIDGET(self->select_button));
    gtk_widget_set_hexpand(iso_box, TRUE);
    gtk_grid_attach(GTK_GRID(drive_grid), iso_box, 1, 2, 3, 1);

    /* Image mode row */
    GtkWidget *mode_label = gtk_label_new("Image mode");
    gtk_widget_set_halign(mode_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(drive_grid), mode_label, 0, 3, 1, 1);

    self->write_mode_dropdown = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(write_mode_options));
    gtk_drop_down_set_selected(self->write_mode_dropdown, 0);
    g_signal_connect(self->write_mode_dropdown, "notify::selected",
                     G_CALLBACK(on_write_mode_changed), self);
    gtk_grid_attach(GTK_GRID(drive_grid), GTK_WIDGET(self->write_mode_dropdown), 1, 3, 3, 1);

    /* Hash row */
    self->hash_label = GTK_LABEL(gtk_label_new(""));
    gtk_widget_set_halign(GTK_WIDGET(self->hash_label), GTK_ALIGN_START);
    gtk_widget_add_css_class(GTK_WIDGET(self->hash_label), "dim-label");
    gtk_grid_attach(GTK_GRID(drive_grid), GTK_WIDGET(self->hash_label), 1, 4, 3, 1);

    /* Partition scheme / Target system row */
    GtkWidget *part_label = gtk_label_new("Partition scheme");
    gtk_widget_set_halign(part_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(drive_grid), part_label, 0, 5, 1, 1);

    self->partition_dropdown = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(partition_options));
    g_signal_connect(self->partition_dropdown, "notify::selected",
                     G_CALLBACK(on_param_changed), self);
    gtk_grid_attach(GTK_GRID(drive_grid), GTK_WIDGET(self->partition_dropdown), 1, 5, 1, 1);

    GtkWidget *target_label = gtk_label_new("Target system");
    gtk_widget_set_halign(target_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(drive_grid), target_label, 2, 5, 1, 1);

    self->target_dropdown = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(target_options));
    gtk_drop_down_set_selected(self->target_dropdown, 2);
    g_signal_connect(self->target_dropdown, "notify::selected",
                     G_CALLBACK(on_param_changed), self);
    gtk_grid_attach(GTK_GRID(drive_grid), GTK_WIDGET(self->target_dropdown), 3, 5, 1, 1);

    GtkWidget *drive_section = create_section("Drive Properties", drive_grid);

    /* === Format Options Section === */
    GtkWidget *format_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(format_grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(format_grid), 8);
    gtk_widget_set_margin_top(format_grid, 8);
    gtk_widget_set_margin_bottom(format_grid, 8);
    gtk_widget_set_margin_start(format_grid, 8);
    gtk_widget_set_margin_end(format_grid, 8);

    GtkWidget *label_label = gtk_label_new("Volume label");
    gtk_widget_set_halign(label_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(format_grid), label_label, 0, 0, 1, 1);

    self->label_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(self->label_entry, "RUFUS_USB");
    gtk_widget_set_hexpand(GTK_WIDGET(self->label_entry), TRUE);
    g_signal_connect(self->label_entry, "notify::text",
                     G_CALLBACK(on_param_changed), self);
    gtk_grid_attach(GTK_GRID(format_grid), GTK_WIDGET(self->label_entry), 1, 0, 3, 1);

    GtkWidget *fs_label = gtk_label_new("File system");
    gtk_widget_set_halign(fs_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(format_grid), fs_label, 0, 1, 1, 1);

    self->fs_dropdown = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(fs_options));
    g_signal_connect(self->fs_dropdown, "notify::selected",
                     G_CALLBACK(on_param_changed), self);
    gtk_grid_attach(GTK_GRID(format_grid), GTK_WIDGET(self->fs_dropdown), 1, 1, 1, 1);

    GtkWidget *cluster_label = gtk_label_new("Cluster size");
    gtk_widget_set_halign(cluster_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(format_grid), cluster_label, 2, 1, 1, 1);

    self->cluster_dropdown = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(cluster_options));
    g_signal_connect(self->cluster_dropdown, "notify::selected",
                     G_CALLBACK(on_param_changed), self);
    gtk_grid_attach(GTK_GRID(format_grid), GTK_WIDGET(self->cluster_dropdown), 3, 1, 1, 1);

    GtkWidget *format_section = create_section("Format Options", format_grid);

    /* === Status Section === */
    GtkWidget *status_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(status_box, 8);
    gtk_widget_set_margin_bottom(status_box, 8);
    gtk_widget_set_margin_start(status_box, 8);
    gtk_widget_set_margin_end(status_box, 8);

    self->progress_bar = GTK_PROGRESS_BAR(gtk_progress_bar_new());
    gtk_progress_bar_set_show_text(self->progress_bar, TRUE);
    gtk_progress_bar_set_text(self->progress_bar, "");
    gtk_box_append(GTK_BOX(status_box), GTK_WIDGET(self->progress_bar));

    self->status_label = GTK_LABEL(gtk_label_new("READY"));
    gtk_widget_set_halign(GTK_WIDGET(self->status_label), GTK_ALIGN_START);
    gtk_widget_add_css_class(GTK_WIDGET(self->status_label), "status-ready");
    gtk_box_append(GTK_BOX(status_box), GTK_WIDGET(self->status_label));

    GtkWidget *status_section = create_section("Status", status_box);

    /* === Button Row === */
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(button_box, GTK_ALIGN_END);

    self->start_button = GTK_BUTTON(gtk_button_new_with_label("Start"));
    gtk_widget_add_css_class(GTK_WIDGET(self->start_button), "suggested-action");
    g_signal_connect(self->start_button, "clicked", G_CALLBACK(on_start_clicked), self);

    self->close_button = GTK_BUTTON(gtk_button_new_with_label("Close"));
    g_signal_connect(self->close_button, "clicked", G_CALLBACK(on_close_clicked), self);

    gtk_box_append(GTK_BOX(button_box), GTK_WIDGET(self->start_button));
    gtk_box_append(GTK_BOX(button_box), GTK_WIDGET(self->close_button));

    /* Assemble main layout */
    gtk_box_append(GTK_BOX(main_box), drive_section);
    gtk_box_append(GTK_BOX(main_box), format_section);
    gtk_box_append(GTK_BOX(main_box), status_section);
    gtk_box_append(GTK_BOX(main_box), button_box);

    gtk_window_set_child(GTK_WINDOW(self), main_box);

    /* Initial state */
    refresh_devices(self);
    update_start_sensitivity(self);
    reset_status_ready(self);

    /* Start device hotplug monitoring */
    hotplug_window = self;
    if (!device_monitor_start(on_device_change, self)) {
        rufus_log("Warning: Device hotplug monitoring not available");
    }
}

RufusWindow *rufus_window_new(RufusApp *app)
{
    return g_object_new(RUFUS_TYPE_WINDOW,
                        "application", app,
                        NULL);
}
