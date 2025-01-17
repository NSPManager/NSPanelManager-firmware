#include <Nextion.hpp>
#include <Nextion_constans.hpp>
#include <Nextion_event.hpp>
#include <esp_event.h>
#include <esp_log.h>
#include <memory.h>
#include <string>
#include <vector>

ESP_EVENT_DEFINE_BASE(NEXTION_EVENT);

esp_err_t Nextion::init() {
  esp_log_level_set("Nextion", esp_log_level_t::ESP_LOG_DEBUG); // TODO: Load from config
  // Setup initial values and create mutexes:
  Nextion::_nextion_state_mutex = xSemaphoreCreateMutex();
  Nextion::_uart_write_mutex = xSemaphoreCreateMutex();
  Nextion::_event_wait_mutex = xSemaphoreCreateMutex();
  Nextion::_event_wait_integer_mutex = xSemaphoreCreateMutex();

  Nextion::_current_nextion_state = nextion_state_t::INITIALIZING;

  // Start custom event loop that handles messages from UART -> data handler
  Nextion::_handle_uart_data_event_loop_args = {
      .queue_size = 16,
      .task_name = "uart_handle_data",
      .task_priority = 8,
      .task_stack_size = 2048,
      .task_core_id = 1,
  };
  esp_err_t create_result = esp_event_loop_create(&Nextion::_handle_uart_data_event_loop_args, &Nextion::_handle_uart_data_event_loop);
  if (create_result != ESP_OK) {
    ESP_LOGE("Nextion", "Failed to create custom event loop to handle uart data. Error: %s", esp_err_to_name(create_result));
    return ESP_ERR_NOT_FINISHED;
  }

  // Configure UART
  Nextion::_uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT};

  uart_driver_install(UART_NUM_2, NEXTION_UART_BUFFER_SIZE * 2, 6000, 8, &Nextion::_uart_event_queue, 0); // Setup UART driver
  uart_param_config(UART_NUM_2, &Nextion::_uart_config);
  uart_set_pin(UART_NUM_2, GPIO_NUM_16, GPIO_NUM_17, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  uart_enable_pattern_det_baud_intr(UART_NUM_2, 0xFF, 3, 9, 0, 0); // Setup pattern detection to trigger interrupt when 3 consecutive 0xFF has been received.
  uart_pattern_queue_reset(UART_NUM_2, 8);

  // Register event handler for when uart data is fully read and processed into an Nextion event
  esp_err_t reg_handler_result = esp_event_handler_register_with(Nextion::_handle_uart_data_event_loop, NEXTION_EVENT, nextion_event_t::RECEIVED_DATA, &Nextion::_uart_data_handler, NULL);
  if (reg_handler_result != ESP_OK) {
    ESP_LOGE("Nextion", "Failed to register Nextion UART data processing handler. Got result %s", esp_err_to_name(reg_handler_result));
  }

  // Start the task tat is responsible for handling Nextion display data
  xTaskCreatePinnedToCore(Nextion::_task_uart_event, "uart_event_task", 4096, NULL, 12, NULL, 1);

  ESP_LOGD("Nextion", "Turning Nextion display off and on again.");
  gpio_set_direction(NEXTION_ON_OFF_GPIO, GPIO_MODE_OUTPUT);
  Nextion::restart();

  // Wait for 'NSPM' flag, if we receive it.
  if (Nextion::_wait_for_event(NEXTION_EVENT, nextion_event_t::RECEIVED_NSPM_FLAG, 2500, 250) == ESP_OK) {
    ESP_LOGI("Nextion", "Received 'NSPM'-flag. You are running the NSPanel Manager GUI/TFT.");
  } else {
    ESP_LOGW("Nextion", "Did not receive the 'NSPM'-flag on screen startup. Are you running the NSPanel Manager GUI/TFT?");
    Nextion::_has_received_nspm_flag = false;
  }

  ESP_LOGI("Nextion", "Trying to connect to Nextion display.");
  Nextion::_write_command("DRAKJHSUYDGBNCJHGJKSHBDN"); // Send a bunch of jibberish to "reset" the Nextion display from any state it is in currently.
  vTaskDelay(pdMS_TO_TICKS(250));                      // Wait for panel to process
  uart_flush(UART_NUM_2);                              // Clear UART buffers

  ESP_LOGD("Nextion", "Sending connect command to panel.");
  if (Nextion::_write_command("connect") != ESP_OK) {
    ESP_LOGE("Nextion", "Failed to send connect command to Nextion display.");
  }

  if (Nextion::_wait_for_event(NEXTION_EVENT, nextion_event_t::CONNECTED, 500, 32) == ESP_OK) {
    ESP_LOGI("Nextion", "Connected to Nextion display.");
  } else {
    ESP_LOGW("Nextion", "Failed to receive 'comok' from Nextion display, will continue anyway.");
  }

  // Set initial values in Nextion display:
  Nextion::_write_command("bkcmd=0");
  Nextion::_write_command("sleep=0");
  Nextion::_write_command("bkcmd=0");
  Nextion::_write_command("sleep=0");

  Nextion::_current_nextion_state = nextion_state_t::RUNNING;
  ESP_LOGI("Nextion", "Nextion init complete.");

  return ESP_OK;
}

void Nextion::_task_uart_event(void *param) {
  ESP_LOGI("Nextion", "Started Nextion uart event handler.");

  uart_event_t event;
  std::vector<char> uart_buffer;
  bool read_nextion_jump_instruction = false;
  std::vector<uint8_t> uart_pattern_buffer(3);
  // std::vector<uint8_t> uart_read_buffer(NEXTION_UART_BUFFER_SIZE);
  for (;;) {
    // Wait for UART data, while waiting pause task
    if (xQueueReceive(Nextion::_uart_event_queue, (void *)&event, portMAX_DELAY)) {
      switch (event.type) {
      case UART_DATA: {
        if (event.size <= NEXTION_UART_BUFFER_SIZE) {
          if (xSemaphoreTake(Nextion::_nextion_state_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
            if (Nextion::_current_nextion_state == nextion_state_t::UPDATING) {
              uart_buffer.resize(event.size);
              uart_read_bytes(UART_NUM_2, uart_buffer.data(), event.size, pdMS_TO_TICKS(64));

              // Special handling for 0x08 jump instruction from Nextion display.
              if (uart_buffer[0] == 0x08) {
                read_nextion_jump_instruction = true;
                xSemaphoreGive(Nextion::_nextion_state_mutex); // Give back mutex as we do a continue here and will not reach the xSemaphoreGive at the bottom.
                continue;                                      // Wait for next event after 0x08 to received the 4 bytes of offset data.
              } else if (read_nextion_jump_instruction) {      // Least event read was a 0x08 jump instruction. Read this data as jump dest and reset.
                if (event.size >= 4) {
                  // Copy all data 1 byte forward in memory and then set data[0] = 0x08
                  uart_buffer.resize(event.size + 1);
                  for (int i = event.size; i > 0; i--) {
                    uart_buffer[i] = uart_buffer[i - 1];
                  }
                  uart_buffer[0] = 0x08;
                }
                read_nextion_jump_instruction = false; // Reset special handling
              }

              switch (esp_event_post_to(Nextion::_handle_uart_data_event_loop, NEXTION_EVENT, nextion_event_t::RECEIVED_DATA, &uart_buffer, sizeof(uart_buffer), pdMS_TO_TICKS(16))) {
              case ESP_OK:
                break;

              case ESP_ERR_TIMEOUT:
                ESP_LOGE("Nextion", "Failed to post new event data from UART event!");
                break;

              case ESP_ERR_INVALID_ARG:
                ESP_LOGE("Nextion", "Invalid combination of event base and data when posting event from UART data!");
                break;

              default:
                ESP_LOGE("Nextion", "Unknown error when posting event data from UART event!");
              }
            }
            xSemaphoreGive(Nextion::_nextion_state_mutex);
          } else {
            ESP_LOGE("Nextion", "Failed to take _nextion_state_mutex while checking current state!");
          }
        }
        break;
      }

      case UART_FIFO_OVF:
        ESP_LOGW("Nextion", "UART buffer overflow, will reset buffers. Any data in buffers or event queue will be lost!");
        uart_flush(UART_NUM_2);
        xQueueReset(Nextion::_uart_event_queue);
        break;

      case UART_BUFFER_FULL:
        ESP_LOGW("Nextion", "UART buffer full! Will reset buffers. Any data in buffers or event queue will be lost.");
        uart_flush(UART_NUM_2);
        xQueueReset(Nextion::_uart_event_queue);
        break;

      case UART_PATTERN_DET: {
        size_t uart_buffered_data_size;
        if (uart_get_buffered_data_len(UART_NUM_2, &uart_buffered_data_size) == ESP_OK) {
          int pattern_position = uart_pattern_pop_pos(UART_NUM_2);
          if (pattern_position == -1) [[unlikely]] {
            ESP_LOGE("Nextion", "Pattern position queue is full. Will clear buffers and queue, all data will be lost!");
            uart_flush(UART_NUM_2);
          } else {
            uart_buffer.resize(pattern_position);
            uart_read_bytes(UART_NUM_2, uart_buffer.data(), pattern_position, pdMS_TO_TICKS(64));
            uart_read_bytes(UART_NUM_2, uart_pattern_buffer.data(), 3, pdMS_TO_TICKS(64)); // Read last three 0XFF as well to clear buffer.

            switch (esp_event_post_to(Nextion::_handle_uart_data_event_loop, NEXTION_EVENT, nextion_event_t::RECEIVED_DATA, &uart_buffer, sizeof(uart_buffer), pdMS_TO_TICKS(16))) {
            case ESP_OK:
              [[likely]];
              break;

            case ESP_ERR_TIMEOUT:
              ESP_LOGE("Nextion", "Failed to post new event data from UART event!");
              break;

            case ESP_ERR_INVALID_ARG:
              ESP_LOGE("Nextion", "Invalid combination of event base and data when posting event from UART data!");
              break;

            default:
              ESP_LOGE("Nextion", "Unknown error when posting event data from UART event!");
            }
          }
        } else [[unlikely]] {
          ESP_LOGE("Nextion", "Failed to get UART data buffered size!");
        }
        break;
      }

      default:
        ESP_LOGW("Nextion", "Unknown UART event type %d!", event.type);
        break;
      }
    }
  }
}

void Nextion::_uart_data_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  std::vector<char> *data = (std::vector<char> *)event_data;
  if (data->size() <= 0) [[unlikely]] {
    return;
  }
  // ESP_LOGD("Nextion", "Read Nextion data: %s, size: %d", data->data, data->data_size);

  if (data->data()[0] == NEX_RET_NUMBER_HEAD) {
    // Got numeric data
    if (data->size() >= 5) [[likely]] {
      if (xSemaphoreTake(Nextion::_event_wait_integer_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (Nextion::_event_wait_integer_result != NULL) {
          *Nextion::_event_wait_integer_result = data->data()[1];
          *Nextion::_event_wait_integer_result += data->data()[2] << 8;
          *Nextion::_event_wait_integer_result += data->data()[3] << 16;
          *Nextion::_event_wait_integer_result += data->data()[4] << 24;
        } else {
          ESP_LOGE("Nextion", "Got integer data but _event_wait_integer_result is NULL. No place to store result!");
        }

        if (Nextion::_event_wait_integer != NULL) {
          xTaskNotifyGive(Nextion::_event_wait_integer);
        } else {
          ESP_LOGE("Nextion", "Got integer data but _event_wait_integer is NULL. No task to notify about result!");
        }
        xSemaphoreGive(Nextion::_event_wait_integer_mutex);
      } else {
        ESP_LOGE("Nextion", "Failed to get _event_wait_integer_mutex while processing integer response.");
        TaskHandle_t holding_task = xSemaphoreGetMutexHolder(Nextion::_event_wait_integer_mutex);
        if (holding_task != NULL) {
          char *task_name = pcTaskGetName(holding_task);
          ESP_LOGE("Nextion", "Mutex held by: %s", task_name);
        }
      }
    } else {
      ESP_LOGE("Nextion", "Not enough bytes sent for 0x71 integer response.");
    }
  } else if (data->data()[0] == NEX_RET_EVENT_TOUCH_HEAD) {
    if (data->size() >= 4) [[likely]] {
      nextion_event_touch_t touch_event;
      touch_event.page_number = data->data()[1];
      touch_event.component_id = data->data()[2];
      touch_event.pressed = data->data()[3];
      esp_event_post(NEXTION_EVENT, nextion_event_t::TOUCH_EVENT, &touch_event, sizeof(touch_event), pdMS_TO_TICKS(16));
    } else {
      ESP_LOGE("Nextion", "Not enough bytes sent for 0x65 touch event.");
    }
  } else if (data->data()[0] == NEX_OUT_SLEEP) {
    esp_event_post(NEXTION_EVENT, nextion_event_t::SLEEP_EVENT, NULL, 0, pdMS_TO_TICKS(5000));
  } else if (data->data()[0] == NEX_OUT_WAKE) {
    esp_event_post(NEXTION_EVENT, nextion_event_t::WAKE_EVENT, NULL, 0, pdMS_TO_TICKS(5000));
  } else if (data->size() >= strlen("NSPM") && strncmp(data->data() + data->size() - strlen("NSPM"), "NSPM", strlen("NSPM")) == 0) { // TODO: Compare last bytes of message instead of first as there may be garbage data output from the panel before sending NSPM-flag
    esp_event_post(NEXTION_EVENT, nextion_event_t::RECEIVED_NSPM_FLAG, NULL, 0, pdMS_TO_TICKS(16));
  } else if (data->size() >= 5 && strncmp(data->data(), "comok", strlen("comok")) == 0) {
    ESP_LOGD("Nextion", "Connected to Nextion display, comok data: %s", data->data());
    esp_event_post(NEXTION_EVENT, nextion_event_t::CONNECTED, NULL, 0, pdMS_TO_TICKS(16));
  } else if (data->data()[0] == 0x05) {
    esp_event_post(NEXTION_EVENT, nextion_event_t::UPDATE_READY_FOR_NEXT_CHUNK, NULL, 0, pdMS_TO_TICKS(1000));
  } else if (data->data()[0] == 0x08) {
    if (data->size() >= 5) [[likely]] {
      size_t byte_offset;
      byte_offset = data->data()[1];
      byte_offset += data->data()[2] << 8;
      byte_offset += data->data()[3] << 16;
      byte_offset += data->data()[4] << 24;

      esp_event_post(NEXTION_EVENT, nextion_event_t::UPDATE_JUMP_TO_OFFSET, &byte_offset, sizeof(byte_offset), pdMS_TO_TICKS(1000));
    } else {
      ESP_LOGE("Nextion", "Not enough bytes sent for 0x08 update byte offset! Only read %zu bytes.", data->size());
    }
  } else {
    ESP_LOGW("Nextion", "Unknown event data from Nextion: %s", data->data());
  }
}

esp_err_t Nextion::_wait_for_event(esp_event_base_t event_base, int32_t event_id, uint16_t timeout, uint16_t mutex_timeout) {
  // Wait for mutex that allows us to manipulate the _event_wait_task task handle
  if (xSemaphoreTake(Nextion::_event_wait_mutex, pdMS_TO_TICKS(mutex_timeout)) == pdTRUE) {
    // Set the waiting task handle to the currently calling task
    if (Nextion::_event_wait_task == NULL) {
      Nextion::_event_wait_task = xTaskGetCurrentTaskHandle();
      esp_event_handler_register(event_base, event_id, Nextion::_wait_for_event_event_handler, NULL);
    }
    xSemaphoreGive(Nextion::_event_wait_mutex);
  } else {
    return ESP_ERR_TIMEOUT;
  }

  // Wait for the notification that the event happened
  BaseType_t result = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout));

  // Reset the waiting task handle to NULL
  if (xSemaphoreTake(Nextion::_event_wait_mutex, portMAX_DELAY) == pdTRUE) {
    Nextion::_event_wait_task = NULL;
    xSemaphoreGive(Nextion::_event_wait_mutex);
  } else {
    return ESP_ERR_TIMEOUT;
  }

  // Unregister event handler for waiting task
  esp_event_handler_unregister(event_base, event_id, Nextion::_wait_for_event_event_handler);

  if (result == pdTRUE) {
    return ESP_OK;
  } else {
    return ESP_ERR_TIMEOUT;
  }
}

void Nextion::_wait_for_event_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  // Handler is set to only trigger on the correct EVENT_BASE and EVENT_ID, simply send a notification to task that event happened
  if (xSemaphoreTake(Nextion::_event_wait_mutex, pdMS_TO_TICKS(32)) == pdTRUE) {
    if (Nextion::_event_wait_task != NULL) { // A task is set, send notification
      xTaskNotifyGive(Nextion::_event_wait_task);
    }
    xSemaphoreGive(Nextion::_event_wait_mutex);
  }
}

esp_err_t Nextion::_write_command(char *data) {
  if (xSemaphoreTake(Nextion::_uart_write_mutex, pdMS_TO_TICKS(32)) == pdTRUE) {
    int len = uart_write_bytes(UART_NUM_2, data, strlen(data));
    // Send command finished sequence
    uint8_t command_end_sequence[3] = {0xFF, 0xFF, 0xFF};
    uart_write_bytes(UART_NUM_2, command_end_sequence, sizeof(command_end_sequence));

    // Give back semaphore
    xSemaphoreGive(Nextion::_uart_write_mutex);
    if (len == strlen(data)) {
      return ESP_OK;
    }
  }
  return ESP_ERR_NOT_FINISHED;
}

esp_err_t Nextion::go_to_page(const char *page, uint16_t mutex_timeout) {
  // Verify that the Nextion state is "running" as we don't want to send data that may interrupt other processes such as updating the GUI
  if (xSemaphoreTake(Nextion::_nextion_state_mutex, pdMS_TO_TICKS(mutex_timeout)) == pdTRUE) {
    if (Nextion::_current_nextion_state != nextion_state_t::RUNNING) {
      xSemaphoreGive(Nextion::_nextion_state_mutex);
      return ESP_ERR_TIMEOUT;
    }
    xSemaphoreGive(Nextion::_nextion_state_mutex);
  }

  if (xSemaphoreTake(Nextion::_uart_write_mutex, pdMS_TO_TICKS(mutex_timeout)) == pdTRUE) {
    uart_write_bytes(UART_NUM_2, "page ", strlen("page "));
    uart_write_bytes(UART_NUM_2, page, strlen(page));

    // Send command finished sequence
    uint8_t command_end_sequence[3] = {0xFF, 0xFF, 0xFF};
    uart_write_bytes(UART_NUM_2, command_end_sequence, sizeof(command_end_sequence));

    xSemaphoreGive(Nextion::_uart_write_mutex);
    return ESP_OK;
  }
  return ESP_ERR_TIMEOUT;
}

esp_err_t Nextion::set_component_text(const char *component_id, const char *text, uint16_t mutex_timeout) {
  // Verify that the Nextion state is "running" as we don't want to send data that may interrupt other processes such as updating the GUI
  if (xSemaphoreTake(Nextion::_nextion_state_mutex, pdMS_TO_TICKS(mutex_timeout)) == pdTRUE) {
    if (Nextion::_current_nextion_state != nextion_state_t::RUNNING) {
      xSemaphoreGive(Nextion::_nextion_state_mutex);
      return ESP_ERR_TIMEOUT;
    }
    xSemaphoreGive(Nextion::_nextion_state_mutex);
  }

  if (xSemaphoreTake(Nextion::_uart_write_mutex, pdMS_TO_TICKS(mutex_timeout)) == pdTRUE) {
    uart_write_bytes(UART_NUM_2, component_id, strlen(component_id));
    uart_write_bytes(UART_NUM_2, ".txt=\"", strlen(".txt=\""));
    uart_write_bytes(UART_NUM_2, text, strlen(text));
    uart_write_bytes(UART_NUM_2, "\"", strlen("\""));

    // Send command finished sequence
    uint8_t command_end_sequence[3] = {0xFF, 0xFF, 0xFF};
    uart_write_bytes(UART_NUM_2, command_end_sequence, sizeof(command_end_sequence));

    xSemaphoreGive(Nextion::_uart_write_mutex);
    return ESP_OK;
  }
  return ESP_ERR_TIMEOUT;
}

esp_err_t Nextion::set_component_value(const char *component_id, int16_t value, uint16_t mutex_timeout) {
  // Verify that the Nextion state is "running" as we don't want to send data that may interrupt other processes such as updating the GUI
  if (xSemaphoreTake(Nextion::_nextion_state_mutex, pdMS_TO_TICKS(mutex_timeout)) == pdTRUE) {
    if (Nextion::_current_nextion_state != nextion_state_t::RUNNING) {
      xSemaphoreGive(Nextion::_nextion_state_mutex);
      return ESP_ERR_TIMEOUT;
    }
    xSemaphoreGive(Nextion::_nextion_state_mutex);
  }

  if (xSemaphoreTake(Nextion::_uart_write_mutex, pdMS_TO_TICKS(mutex_timeout)) == pdTRUE) {
    uart_write_bytes(UART_NUM_2, component_id, strlen(component_id));
    uart_write_bytes(UART_NUM_2, ".val=", strlen(".val="));
    std::string value_string = std::to_string(value);
    uart_write_bytes(UART_NUM_2, value_string.c_str(), value_string.length());

    // Send command finished sequence
    uint8_t command_end_sequence[3] = {0xFF, 0xFF, 0xFF};
    uart_write_bytes(UART_NUM_2, command_end_sequence, sizeof(command_end_sequence));

    xSemaphoreGive(Nextion::_uart_write_mutex);
    return ESP_OK;
  }
  return ESP_ERR_TIMEOUT;
}

esp_err_t Nextion::set_timer_value(const char *component_id, uint16_t value, uint16_t mutex_timeout) {
  // Verify that the Nextion state is "running" as we don't want to send data that may interrupt other processes such as updating the GUI
  if (xSemaphoreTake(Nextion::_nextion_state_mutex, pdMS_TO_TICKS(mutex_timeout)) == pdTRUE) {
    if (Nextion::_current_nextion_state != nextion_state_t::RUNNING) {
      xSemaphoreGive(Nextion::_nextion_state_mutex);
      return ESP_ERR_TIMEOUT;
    }
    xSemaphoreGive(Nextion::_nextion_state_mutex);
  }

  if (xSemaphoreTake(Nextion::_uart_write_mutex, pdMS_TO_TICKS(mutex_timeout)) == pdTRUE) {
    uart_write_bytes(UART_NUM_2, component_id, strlen(component_id));
    uart_write_bytes(UART_NUM_2, ".tim=", strlen(".tim="));
    std::string value_string = std::to_string(value);
    uart_write_bytes(UART_NUM_2, value_string.c_str(), value_string.length());

    // Send command finished sequence
    uint8_t command_end_sequence[3] = {0xFF, 0xFF, 0xFF};
    uart_write_bytes(UART_NUM_2, command_end_sequence, sizeof(command_end_sequence));

    xSemaphoreGive(Nextion::_uart_write_mutex);
    return ESP_OK;
  }
  return ESP_ERR_TIMEOUT;
}

esp_err_t Nextion::set_component_pic(const char *component_id, uint8_t value, uint16_t mutex_timeout) {
  // Verify that the Nextion state is "running" as we don't want to send data that may interrupt other processes such as updating the GUI
  if (xSemaphoreTake(Nextion::_nextion_state_mutex, pdMS_TO_TICKS(mutex_timeout)) == pdTRUE) {
    if (Nextion::_current_nextion_state != nextion_state_t::RUNNING) {
      xSemaphoreGive(Nextion::_nextion_state_mutex);
      return ESP_ERR_TIMEOUT;
    }
    xSemaphoreGive(Nextion::_nextion_state_mutex);
  }

  if (xSemaphoreTake(Nextion::_uart_write_mutex, pdMS_TO_TICKS(mutex_timeout)) == pdTRUE) {
    uart_write_bytes(UART_NUM_2, component_id, strlen(component_id));
    uart_write_bytes(UART_NUM_2, ".pic=", strlen(".pic="));
    std::string value_string = std::to_string(value);
    uart_write_bytes(UART_NUM_2, value_string.c_str(), value_string.length());

    // Send command finished sequence
    uint8_t command_end_sequence[3] = {0xFF, 0xFF, 0xFF};
    uart_write_bytes(UART_NUM_2, command_end_sequence, sizeof(command_end_sequence));

    xSemaphoreGive(Nextion::_uart_write_mutex);
    return ESP_OK;
  }
  return ESP_ERR_TIMEOUT;
}

esp_err_t Nextion::set_component_pic1(const char *component_id, uint8_t value, uint16_t mutex_timeout) {
  // Verify that the Nextion state is "running" as we don't want to send data that may interrupt other processes such as updating the GUI
  if (xSemaphoreTake(Nextion::_nextion_state_mutex, pdMS_TO_TICKS(mutex_timeout)) == pdTRUE) {
    if (Nextion::_current_nextion_state != nextion_state_t::RUNNING) {
      xSemaphoreGive(Nextion::_nextion_state_mutex);
      return ESP_ERR_TIMEOUT;
    }
    xSemaphoreGive(Nextion::_nextion_state_mutex);
  }

  if (xSemaphoreTake(Nextion::_uart_write_mutex, pdMS_TO_TICKS(mutex_timeout)) == pdTRUE) {
    uart_write_bytes(UART_NUM_2, component_id, strlen(component_id));
    uart_write_bytes(UART_NUM_2, ".pic1=", strlen(".pic1="));
    std::string value_string = std::to_string(value);
    uart_write_bytes(UART_NUM_2, value_string.c_str(), value_string.length());

    // Send command finished sequence
    uint8_t command_end_sequence[3] = {0xFF, 0xFF, 0xFF};
    uart_write_bytes(UART_NUM_2, command_end_sequence, sizeof(command_end_sequence));

    xSemaphoreGive(Nextion::_uart_write_mutex);
    return ESP_OK;
  }
  return ESP_ERR_TIMEOUT;
}

esp_err_t Nextion::set_component_foreground(const char *component_id, uint16_t color, uint16_t mutex_timeout) {
  // Verify that the Nextion state is "running" as we don't want to send data that may interrupt other processes such as updating the GUI
  if (xSemaphoreTake(Nextion::_nextion_state_mutex, pdMS_TO_TICKS(mutex_timeout)) == pdTRUE) {
    if (Nextion::_current_nextion_state != nextion_state_t::RUNNING) {
      xSemaphoreGive(Nextion::_nextion_state_mutex);
      return ESP_ERR_TIMEOUT;
    }
    xSemaphoreGive(Nextion::_nextion_state_mutex);
  }

  if (xSemaphoreTake(Nextion::_uart_write_mutex, pdMS_TO_TICKS(mutex_timeout)) == pdTRUE) {
    uart_write_bytes(UART_NUM_2, component_id, strlen(component_id));
    uart_write_bytes(UART_NUM_2, ".pco=", strlen(".pco="));
    std::string value_string = std::to_string(color);
    uart_write_bytes(UART_NUM_2, value_string.c_str(), value_string.length());

    // Send command finished sequence
    uint8_t command_end_sequence[3] = {0xFF, 0xFF, 0xFF};
    uart_write_bytes(UART_NUM_2, command_end_sequence, sizeof(command_end_sequence));

    xSemaphoreGive(Nextion::_uart_write_mutex);
    return ESP_OK;
  }
  return ESP_ERR_TIMEOUT;
}

esp_err_t Nextion::set_component_pco2(const char *component_id, uint16_t color, uint16_t mutex_timeout) {
  // Verify that the Nextion state is "running" as we don't want to send data that may interrupt other processes such as updating the GUI
  if (xSemaphoreTake(Nextion::_nextion_state_mutex, pdMS_TO_TICKS(mutex_timeout)) == pdTRUE) {
    if (Nextion::_current_nextion_state != nextion_state_t::RUNNING) {
      xSemaphoreGive(Nextion::_nextion_state_mutex);
      return ESP_ERR_TIMEOUT;
    }
    xSemaphoreGive(Nextion::_nextion_state_mutex);
  }

  if (xSemaphoreTake(Nextion::_uart_write_mutex, pdMS_TO_TICKS(mutex_timeout)) == pdTRUE) {
    uart_write_bytes(UART_NUM_2, component_id, strlen(component_id));
    uart_write_bytes(UART_NUM_2, ".pco2=", strlen(".pco2="));
    std::string value_string = std::to_string(color);
    uart_write_bytes(UART_NUM_2, value_string.c_str(), value_string.length());

    // Send command finished sequence
    uint8_t command_end_sequence[3] = {0xFF, 0xFF, 0xFF};
    uart_write_bytes(UART_NUM_2, command_end_sequence, sizeof(command_end_sequence));

    xSemaphoreGive(Nextion::_uart_write_mutex);
    return ESP_OK;
  }
  return ESP_ERR_TIMEOUT;
}

esp_err_t Nextion::set_component_visibility(const char *component_id, bool visibility, uint16_t mutex_timeout) {
  // Verify that the Nextion state is "running" as we don't want to send data that may interrupt other processes such as updating the GUI
  if (xSemaphoreTake(Nextion::_nextion_state_mutex, pdMS_TO_TICKS(mutex_timeout)) == pdTRUE) {
    if (Nextion::_current_nextion_state != nextion_state_t::RUNNING) {
      xSemaphoreGive(Nextion::_nextion_state_mutex);
      return ESP_ERR_TIMEOUT;
    }
    xSemaphoreGive(Nextion::_nextion_state_mutex);
  }

  if (xSemaphoreTake(Nextion::_uart_write_mutex, pdMS_TO_TICKS(mutex_timeout)) == pdTRUE) {
    uart_write_bytes(UART_NUM_2, "vis ", strlen("vis "));
    uart_write_bytes(UART_NUM_2, component_id, strlen(component_id));
    uart_write_bytes(UART_NUM_2, visibility ? ",1" : ",0", strlen(visibility ? ",1" : ",0"));

    // Send command finished sequence
    uint8_t command_end_sequence[3] = {0xFF, 0xFF, 0xFF};
    uart_write_bytes(UART_NUM_2, command_end_sequence, sizeof(command_end_sequence));

    xSemaphoreGive(Nextion::_uart_write_mutex);
    return ESP_OK;
  }
  return ESP_ERR_TIMEOUT;
}

esp_err_t Nextion::get_component_integer_value(const char *component_id, int32_t *result, uint16_t timeout, uint16_t mutex_timeout) {
  // Verify that the Nextion state is "running" as we don't want to send data that may interrupt other processes such as updating the GUI
  if (xSemaphoreTake(Nextion::_nextion_state_mutex, pdMS_TO_TICKS(mutex_timeout)) == pdTRUE) {
    if (Nextion::_current_nextion_state != nextion_state_t::RUNNING) {
      xSemaphoreGive(Nextion::_nextion_state_mutex);
      return ESP_ERR_TIMEOUT;
    }
    xSemaphoreGive(Nextion::_nextion_state_mutex);
  }

  if (xSemaphoreTake(Nextion::_event_wait_integer_mutex, pdMS_TO_TICKS(mutex_timeout)) == pdTRUE) {
    if (Nextion::_event_wait_integer == NULL) {
      Nextion::_event_wait_integer = xTaskGetCurrentTaskHandle();
      Nextion::_event_wait_integer_result = result;
      xSemaphoreGive(Nextion::_event_wait_integer_mutex);
    } else {
      ESP_LOGE("Nextion", "Got mutex but _event_wait_integer is not NULL while fetching integer, cancel operation.");
      xSemaphoreGive(Nextion::_event_wait_integer_mutex);
      return ESP_ERR_TIMEOUT;
    }
  } else {
    ESP_LOGE("Nextion", "Failed to take _event_wait_integer_mutex while fetching integer value.");
    return ESP_ERR_TIMEOUT;
  }

  if (xSemaphoreTake(Nextion::_uart_write_mutex, pdMS_TO_TICKS(mutex_timeout)) == pdTRUE) {
    uart_write_bytes(UART_NUM_2, "get ", strlen("get "));
    uart_write_bytes(UART_NUM_2, component_id, strlen(component_id));
    uart_write_bytes(UART_NUM_2, ".val", strlen(".val"));

    // Send command finished sequence
    uint8_t command_end_sequence[3] = {0xFF, 0xFF, 0xFF};
    if (uart_write_bytes(UART_NUM_2, command_end_sequence, sizeof(command_end_sequence)) != 3) {
      ESP_LOGE("Nextion", "Failed to write all the command end bytes to UART when fetching integer value.");
    }

    xSemaphoreGive(Nextion::_uart_write_mutex);
  } else {
    ESP_LOGE("Nextion", "Failed to take _uart_write_mutex while fetching integer value.");
    Nextion::_event_wait_integer = NULL;
    Nextion::_event_wait_integer_result = NULL;
    return ESP_ERR_TIMEOUT;
  }

  // Wait for result
  BaseType_t wait_result = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout));

  if (xSemaphoreTake(Nextion::_event_wait_integer_mutex, portMAX_DELAY) == pdTRUE) {
    Nextion::_event_wait_integer = NULL;
    Nextion::_event_wait_integer_result = NULL;
    xSemaphoreGive(Nextion::_event_wait_integer_mutex);
  }

  if (wait_result == pdTRUE) {
    return ESP_OK;
  } else {
    return ESP_ERR_TIMEOUT;
  }
}

esp_err_t Nextion::set_brightness_level(uint8_t brightness, uint16_t mutex_timeout) {
  // Verify that the Nextion state is "running" as we don't want to send data that may interrupt other processes such as updating the GUI
  if (xSemaphoreTake(Nextion::_nextion_state_mutex, pdMS_TO_TICKS(mutex_timeout)) == pdTRUE) {
    if (Nextion::_current_nextion_state != nextion_state_t::RUNNING) {
      xSemaphoreGive(Nextion::_nextion_state_mutex);
      return ESP_ERR_TIMEOUT;
    }
    xSemaphoreGive(Nextion::_nextion_state_mutex);
  }

  if (xSemaphoreTake(Nextion::_uart_write_mutex, pdMS_TO_TICKS(mutex_timeout)) == pdTRUE) {
    uart_write_bytes(UART_NUM_2, "dim=", strlen("dim="));
    std::string value_string = std::to_string(brightness);
    uart_write_bytes(UART_NUM_2, value_string.c_str(), value_string.length());

    // Send command finished sequence
    uint8_t command_end_sequence[3] = {0xFF, 0xFF, 0xFF};
    uart_write_bytes(UART_NUM_2, command_end_sequence, sizeof(command_end_sequence));

    xSemaphoreGive(Nextion::_uart_write_mutex);
    return ESP_OK;
  }
  return ESP_ERR_TIMEOUT;
}

nextion_state_t Nextion::get_current_state() {
  return Nextion::_current_nextion_state;
}

void Nextion::restart() {
  gpio_set_level(NEXTION_ON_OFF_GPIO, 1); // Turn off power to the display
  vTaskDelay(pdMS_TO_TICKS(250));         // Wait for screen to turn off
  gpio_set_level(NEXTION_ON_OFF_GPIO, 0); // Turn on power to the display
}

esp_err_t Nextion::start_update(uint32_t upload_baudrate, bool use_new_upload_protocol, uint64_t upload_file_size) {
  if (xSemaphoreTake(Nextion::_uart_write_mutex, pdMS_TO_TICKS(10000)) == pdTRUE) {
    if (xSemaphoreTake(Nextion::_nextion_state_mutex, pdMS_TO_TICKS(10000)) == pdTRUE) {
      Nextion::_current_nextion_state = nextion_state_t::UPDATING; // Set state to updating so that other functions won't write to the UART.
      xSemaphoreGive(Nextion::_nextion_state_mutex);
    } else {
      ESP_LOGE("Nextion", "Failed to take _nextion_state_mutex while processing request to start update!");
      xSemaphoreGive(Nextion::_uart_write_mutex);
      return ESP_ERR_NOT_FINISHED;
    }
    Nextion::restart();

    // Wait for panel to start
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGI("Nextion", "Will try to init Nextion update process. New TFT file size: %llu", upload_file_size);
    std::string init_upload_command = use_new_upload_protocol ? "whmi-wris " : "whmi-wri ";
    init_upload_command.append(std::to_string(upload_file_size));
    init_upload_command.append(",");
    init_upload_command.append(std::to_string(upload_baudrate));
    init_upload_command.append(",1");
    uart_write_bytes(UART_NUM_2, init_upload_command.c_str(), init_upload_command.length());

    ESP_LOGD("Nextion", "Sending update init command to Nextion display: %s", init_upload_command.c_str());

    uint8_t command_end_sequence[3] = {0xFF, 0xFF, 0xFF};
    uart_write_bytes(UART_NUM_2, command_end_sequence, sizeof(command_end_sequence));

    uart_wait_tx_done(UART_NUM_2, pdMS_TO_TICKS(1000)); // Wait up to 1 second for TX buffer to clear.

    // Switch uart to desired baud
    if (uart_set_baudrate(UART_NUM_2, upload_baudrate) != ESP_OK) {
      ESP_LOGE("Nextion", "Failed to set UART baudrate %lu when initializing Nextion update!", upload_baudrate);
    }

    xSemaphoreGive(Nextion::_uart_write_mutex);
    return ESP_OK;
  } else {
    ESP_LOGE("Nextion", "Failed to take _uart_write_mutex while trying to start update to panel.");
  }
  return ESP_ERR_NOT_FINISHED;
}

esp_err_t Nextion::write_update_bytes(uint8_t *data, uint16_t size) {
  if (xSemaphoreTake(Nextion::_uart_write_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    bool write_success = uart_write_bytes(UART_NUM_2, data, size) >= 0;
    uart_wait_tx_done(UART_NUM_2, pdMS_TO_TICKS(1000)); // Wait up to 1 second for TX buffer to clear.

    xSemaphoreGive(Nextion::_uart_write_mutex);
    return write_success ? ESP_OK : ESP_ERR_NOT_FINISHED;
  } else {
    ESP_LOGE("Nextion", "Failed to take _uart_write_mutex while trying to start update to panel.");
  }
  return ESP_ERR_NOT_FINISHED;
}