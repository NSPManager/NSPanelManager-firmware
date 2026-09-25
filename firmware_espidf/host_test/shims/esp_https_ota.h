#pragma once
#include "esp_err.h"
#include "esp_http_client.h"
typedef struct esp_https_ota_handle *esp_https_ota_handle_t;
typedef struct { const esp_http_client_config_t *http_config; http_event_handle_cb http_client_init_cb; bool partial_http_download; int max_http_request_size; } esp_https_ota_config_t;
esp_err_t esp_https_ota(const esp_https_ota_config_t *config);
esp_err_t esp_https_ota_begin(const esp_https_ota_config_t *config, esp_https_ota_handle_t *handle);
esp_err_t esp_https_ota_perform(esp_https_ota_handle_t handle);
bool esp_https_ota_is_complete_data_received(esp_https_ota_handle_t handle);
esp_err_t esp_https_ota_finish(esp_https_ota_handle_t handle);
esp_err_t esp_https_ota_abort(esp_https_ota_handle_t handle);
int esp_https_ota_get_image_len_read(esp_https_ota_handle_t handle);
int esp_https_ota_get_image_size(esp_https_ota_handle_t handle);
#define ESP_ERR_HTTPS_OTA_BASE 0x1900
#define ESP_ERR_HTTPS_OTA_IN_PROGRESS (ESP_ERR_HTTPS_OTA_BASE + 1)
#include "esp_ota_ops.h"
esp_err_t esp_https_ota_get_img_desc(esp_https_ota_handle_t handle, esp_app_desc_t *new_app_info);
