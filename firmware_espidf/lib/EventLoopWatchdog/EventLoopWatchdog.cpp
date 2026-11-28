#include <EventLoopWatchdog.hpp>
#include <MqttManager.hpp>
#include <PanelRestart.hpp>
#include <esp_debug_helpers.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>
#include <string>

ESP_EVENT_DEFINE_BASE(EVENTLOOPWATCHDOG_EVENT);

void EventLoopWatchdog::init() {
  EventLoopWatchdog::_last_handled_us = esp_timer_get_time();
  if (esp_event_handler_register(EVENTLOOPWATCHDOG_EVENT, 0, &EventLoopWatchdog::_probe_handler, NULL) != ESP_OK) [[unlikely]] {
    ESP_LOGE("LoopWatchdog", "Failed to register the probe handler.");
    return;
  }
  // Low priority, like the other helpers that post to the default loop, so the Wi-Fi task wins any tie.
  if (xTaskCreatePinnedToCore(&EventLoopWatchdog::_task_watch, "loop_watchdog", 4096, NULL, 2, NULL, 1) != pdPASS) [[unlikely]] {
    ESP_LOGE("LoopWatchdog", "Failed to create the watchdog task.");
  }
  // Its own task: it blocks on the API lock while MQTT is stuck, and the watchdog task must not.
  if (xTaskCreatePinnedToCore(&EventLoopWatchdog::_task_probe_mqtt, "mqtt_watchdog", 2048, NULL, 2, NULL, 1) != pdPASS) [[unlikely]] {
    ESP_LOGE("LoopWatchdog", "Failed to create the MQTT probe task.");
  }
}

void EventLoopWatchdog::_probe_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  EventLoopWatchdog::_last_handled_us = esp_timer_get_time();
}

void EventLoopWatchdog::_task_probe_mqtt(void *param) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(EventLoopWatchdog::mqtt_probe_interval_ms));
    if (!MqttManager::connected()) {
      continue; // esp-mqtt holds the lock while it connects. Only a client that runs its handlers can be stuck in one.
    }
    EventLoopWatchdog::_mqtt_probe_started_us = esp_timer_get_time();
    MqttManager::probe_api_lock();
    EventLoopWatchdog::_mqtt_probe_started_us = 0;
  }
}

void EventLoopWatchdog::_task_watch(void *param) {
  bool reported = false;
  bool stored_stall_reported = false;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(EventLoopWatchdog::probe_interval_ms));

    int64_t mqtt_probe_started = EventLoopWatchdog::_mqtt_probe_started_us;
    if (mqtt_probe_started != 0) {
      int64_t stuck_ms = (esp_timer_get_time() - mqtt_probe_started) / 1000;
      if (stuck_ms >= EventLoopWatchdog::mqtt_stall_after_ms) {
        EventLoopWatchdog::_handle_mqtt_stall(stuck_ms);
      }
    }
    if (!stored_stall_reported && MqttManager::connected()) {
      stored_stall_reported = true;
      EventLoopWatchdog::_report_stored_mqtt_stall();
    }

    // Timeout 0: only take a slot that is already free. Waiting on the queue of a stalled loop would
    // stall this task too, and it would never report.
    esp_event_post(EVENTLOOPWATCHDOG_EVENT, 0, NULL, 0, 0);

    int64_t since_ms = (esp_timer_get_time() - EventLoopWatchdog::_last_handled_us) / 1000;
    if (since_ms < EventLoopWatchdog::loop_stall_after_ms) {
      if (reported) {
        ESP_LOGW("LoopWatchdog", "Default event loop is dispatching again.");
        reported = false;
      }
      continue;
    }
    if (reported) {
      continue;
    }
    reported = true;
    ESP_LOGE("LoopWatchdog", "Default event loop has not dispatched for %lld ms. Task backtraces follow.", since_ms);
    EventLoopWatchdog::_log_all_backtraces();
  }
}

void EventLoopWatchdog::_capture_putc(char c) {
  if (EventLoopWatchdog::_capture_len < EventLoopWatchdog::capture_size - 1) {
    EventLoopWatchdog::_capture[EventLoopWatchdog::_capture_len++] = c;
  }
}

std::vector<std::string> EventLoopWatchdog::_capture_all_backtraces() {
  std::vector<std::string> lines;
  EventLoopWatchdog::_capture = (char *)heap_caps_malloc(EventLoopWatchdog::capture_size, MALLOC_CAP_8BIT);
  if (EventLoopWatchdog::_capture == nullptr) {
    lines.push_back("No memory for the backtrace capture.");
    return lines;
  }
  EventLoopWatchdog::_capture_len = 0;

  // esp_backtrace_print_all_tasks() writes with esp_rom_printf(), which goes to the UART (channel 1)
  // and to channel 2 if one is installed. Nothing in this firmware installs channel 2, so borrow it.
  esp_rom_install_channel_putc(2, &EventLoopWatchdog::_capture_putc);
  esp_err_t err = esp_backtrace_print_all_tasks(EventLoopWatchdog::backtrace_depth);
  esp_rom_install_channel_putc(2, NULL);
  EventLoopWatchdog::_capture[EventLoopWatchdog::_capture_len] = '\0';

  if (err != ESP_OK) {
    lines.push_back(std::string("Some backtraces are incomplete: ") + esp_err_to_name(err));
  }

  // The output is a task name on one line, then "Backtrace: pc:sp pc:sp ..." on the next. Join them.
  std::string task_name;
  char *saveptr = nullptr;
  for (char *line = strtok_r(EventLoopWatchdog::_capture, "\r\n", &saveptr); line != nullptr; line = strtok_r(nullptr, "\r\n", &saveptr)) {
    while (*line == ' ') {
      line++;
    }
    if (*line == '\0') {
      continue;
    }
    if (strncmp(line, "Backtrace:", strlen("Backtrace:")) == 0) {
      lines.push_back(task_name + " " + line);
      task_name.clear();
    } else {
      task_name = line;
    }
  }
  if (EventLoopWatchdog::_capture_len >= EventLoopWatchdog::capture_size - 1) {
    lines.push_back("Backtrace capture was truncated at " + std::to_string(EventLoopWatchdog::capture_size) + " bytes.");
  }

  free(EventLoopWatchdog::_capture);
  EventLoopWatchdog::_capture = nullptr;
  return lines;
}

void EventLoopWatchdog::_log_all_backtraces() {
  for (const std::string &line : EventLoopWatchdog::_capture_all_backtraces()) {
    ESP_LOGE("LoopWatchdog", "%s", line.c_str());
    // The log queue is small and drops when full. Let the forwarder keep up.
    vTaskDelay(pdMS_TO_TICKS(150));
  }
  ESP_LOGE("LoopWatchdog", "End of task backtraces.");
}

void EventLoopWatchdog::_handle_mqtt_stall(int64_t stuck_ms) {
  // Logging would only queue lines for the MQTT log forwarder, which is stuck too. Print to the serial port.
  std::string header = "MQTT client stuck: its API lock has been held for " + std::to_string(stuck_ms) +
                       " ms, so nothing can be published. A handler on the MQTT task never returns. Task backtraces follow.";
  std::vector<std::string> lines = EventLoopWatchdog::_capture_all_backtraces();
  printf("E LoopWatchdog: %s\n", header.c_str());
  for (const std::string &line : lines) {
    printf("E LoopWatchdog: %s\n", line.c_str());
  }

  FILE *file = fopen(EventLoopWatchdog::mqtt_stall_path, "w");
  if (file != nullptr) {
    fprintf(file, "%s\n", header.c_str());
    for (const std::string &line : lines) {
      fprintf(file, "%s\n", line.c_str());
    }
    fclose(file);
  } else {
    printf("E LoopWatchdog: Could not store the backtraces in %s.\n", EventLoopWatchdog::mqtt_stall_path);
  }

  printf("E LoopWatchdog: Restarting to bring MQTT back.\n");
  fflush(stdout);
  PanelRestart::restart();
}

void EventLoopWatchdog::_report_stored_mqtt_stall() {
  FILE *file = fopen(EventLoopWatchdog::mqtt_stall_path, "r");
  if (file == nullptr) {
    return; // No stall before the last restart
  }
  ESP_LOGE("LoopWatchdog", "MQTT got stuck before the last restart. What was stored then follows.");
  char line[512];
  while (fgets(line, sizeof(line), file) != nullptr) {
    line[strcspn(line, "\r\n")] = '\0';
    if (line[0] != '\0') {
      ESP_LOGE("LoopWatchdog", "%s", line);
      vTaskDelay(pdMS_TO_TICKS(150)); // The log queue is small and drops when full
    }
  }
  fclose(file);
  ESP_LOGE("LoopWatchdog", "End of task backtraces.");
  ::remove(EventLoopWatchdog::mqtt_stall_path);
}
