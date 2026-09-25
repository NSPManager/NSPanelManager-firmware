#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
typedef struct { const char *base_path; const char *partition_label; bool format_if_mount_failed; bool dont_mount; } esp_vfs_littlefs_conf_t;
esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *conf);
esp_err_t esp_vfs_littlefs_unregister(const char *partition_label);
esp_err_t esp_littlefs_info(const char *partition_label, size_t *total, size_t *used);
esp_err_t esp_littlefs_format(const char *partition_label);
