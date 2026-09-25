#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include "esp_partition.h"
typedef uint32_t esp_ota_handle_t;
typedef struct { uint32_t magic_word; uint32_t secure_version; char version[32]; char project_name[32]; char time[16]; char date[16]; char idf_ver[32]; uint8_t app_elf_sha256[32]; } esp_app_desc_t;
const esp_partition_t *esp_ota_get_running_partition(void);
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *start_from);
esp_err_t esp_ota_begin(const esp_partition_t *partition, size_t image_size, esp_ota_handle_t *out_handle);
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t size);
esp_err_t esp_ota_end(esp_ota_handle_t handle);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void);
