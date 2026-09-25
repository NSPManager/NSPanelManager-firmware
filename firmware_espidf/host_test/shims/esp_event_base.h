#pragma once
#include "esp_err.h"
typedef const char *esp_event_base_t;
typedef void *esp_event_loop_handle_t;
typedef void (*esp_event_handler_t)(void *handler_arg, esp_event_base_t base, int32_t id, void *event_data);
typedef void *esp_event_handler_instance_t;
#define ESP_EVENT_ANY_BASE NULL
#define ESP_EVENT_ANY_ID -1
#define ESP_EVENT_DECLARE_BASE(id) extern esp_event_base_t id
#define ESP_EVENT_DEFINE_BASE(id) esp_event_base_t id = #id
