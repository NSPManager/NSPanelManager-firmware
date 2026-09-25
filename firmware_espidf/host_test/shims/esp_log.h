#pragma once
#include <stdio.h>
typedef enum { ESP_LOG_NONE, ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO, ESP_LOG_DEBUG, ESP_LOG_VERBOSE } esp_log_level_t;
void esp_log_level_set(const char *tag, esp_log_level_t level);
#define ESP_LOGE(tag, fmt, ...) do { } while (0)
#define ESP_LOGW(tag, fmt, ...) do { } while (0)
#define ESP_LOGI(tag, fmt, ...) do { } while (0)
#define ESP_LOGD(tag, fmt, ...) do { } while (0)
#define ESP_LOGV(tag, fmt, ...) do { } while (0)
typedef int (*vprintf_like_t)(const char *, __builtin_va_list);
vprintf_like_t esp_log_set_vprintf(vprintf_like_t func);
