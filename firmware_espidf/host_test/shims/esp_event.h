#pragma once
#include "esp_err.h"
#include "esp_event_base.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

typedef struct {
  int32_t queue_size;
  const char *task_name;
  UBaseType_t task_priority;
  uint32_t task_stack_size;
  BaseType_t task_core_id;
} esp_event_loop_args_t;

esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_loop_create(const esp_event_loop_args_t *args, esp_event_loop_handle_t *out);
esp_err_t esp_event_loop_delete(esp_event_loop_handle_t loop);
esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id, esp_event_handler_t handler, void *arg);
esp_err_t esp_event_handler_unregister(esp_event_base_t base, int32_t id, esp_event_handler_t handler);
esp_err_t esp_event_handler_register_with(esp_event_loop_handle_t loop, esp_event_base_t base, int32_t id, esp_event_handler_t handler, void *arg);
esp_err_t esp_event_handler_unregister_with(esp_event_loop_handle_t loop, esp_event_base_t base, int32_t id, esp_event_handler_t handler);
esp_err_t esp_event_post(esp_event_base_t base, int32_t id, const void *data, size_t data_size, TickType_t ticks);
esp_err_t esp_event_post_to(esp_event_loop_handle_t loop, esp_event_base_t base, int32_t id, const void *data, size_t data_size, TickType_t ticks);
