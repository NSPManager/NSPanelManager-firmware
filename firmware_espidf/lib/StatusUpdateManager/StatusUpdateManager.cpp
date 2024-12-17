#include <ConfigManager.hpp>
#include <MqttManager.hpp>
#include <NSPM_ConfigManager.hpp>
#include <NSPM_ConfigManager_event.hpp>
#include <StatusUpdateManager.hpp>
#include <UpdateManager_event.hpp>
#include <WiFiManager.hpp>
#include <cmath>
#include <driver/adc.h>
#include <esp_adc_cal.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <esp_wifi.h>

void StatusUpdateManager::init() {
  esp_log_level_set("StatusUpdateManager", esp_log_level_t::ESP_LOG_DEBUG); // TODO: Load from config

  esp_event_handler_register(NSPM_CONFIGMANAGER_EVENT, ESP_EVENT_ANY_ID, StatusUpdateManager::_event_handler, NULL);
  esp_event_handler_register(UPDATEMANAGER_EVENT, ESP_EVENT_ANY_ID, StatusUpdateManager::_update_manager_event_handler, NULL);

  // Setup Mutex and default values before starting timers/tasks that handle _status_report
  StatusUpdateManager::_status_report_mutex = xSemaphoreCreateMutex();
  nspanel_status_report__init(&StatusUpdateManager::_status_report);
  StatusUpdateManager::_status_report.nspanel_state = NSPanelStatusReport__State::NSPANEL_STATUS_REPORT__STATE__ONLINE;
  StatusUpdateManager::_status_report.update_progress = 0;
  StatusUpdateManager::_status_report.rssi = 0;
  StatusUpdateManager::_status_report.temperature = 0;
  StatusUpdateManager::_status_report.mac_address = (char *)WiFiManager::mac_string();

  // Create status update timer
  esp_err_t err = esp_timer_create(&StatusUpdateManager::_status_update_timer_args, &StatusUpdateManager::_status_update_timer);
  if (err != ESP_OK) {
    ESP_LOGE("StatusUpdateManager", "Failed to start ESP timer to periodically send status updates! Error: %s", esp_err_to_name(err));
  }

  // Create temperature measuring timer
  err = esp_timer_create(&StatusUpdateManager::_measure_temperature_timer_args, &StatusUpdateManager::_measure_temperature_timer);
  if (err != ESP_OK) {
    ESP_LOGE("StatusUpdateManager", "Failed to start ESP timer to periodically measure temperature! Error: %s", esp_err_to_name(err));
  }

  // Setup ADC for reading temperature
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_2, ADC_ATTEN_DB_0);
  StatusUpdateManager::_adc_chars = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_0, ADC_WIDTH_BIT_12, 0, StatusUpdateManager::_adc_chars);

  err = esp_timer_start_periodic(StatusUpdateManager::_status_update_timer, 30000 * 1000); // Send status update every 30 seconds
  if (err != ESP_OK) {
    ESP_LOGE("StatusUpdateManager", "Failed to start periodic timer for sending status updates! Error: %s", esp_err_to_name(err));
  }

  err = esp_timer_start_periodic(StatusUpdateManager::_measure_temperature_timer, 1000 * 1000); // Measure temperature every second
  if (err != ESP_OK) {
    ESP_LOGE("StatusUpdateManager", "Failed to start periodic timer for measuring temperature! Error: %s", esp_err_to_name(err));
  }
}

void StatusUpdateManager::_send_status_update(void *arg) {
  int current_wifi_rssi;
  if (esp_wifi_sta_get_rssi(&current_wifi_rssi) == ESP_OK) {
    StatusUpdateManager::_status_report.rssi = current_wifi_rssi;
  } else {
    ESP_LOGW("StatusUpdateManager", "Failed to get current wifi RSSI. Will use old value.");
  }

  size_t packed_data_size = 0;
  std::vector<uint8_t> buffer;
  if (xSemaphoreTake(StatusUpdateManager::_status_report_mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
    size_t used_heap_size = heap_caps_get_total_size(MALLOC_CAP_DEFAULT) - xPortGetFreeHeapSize();
    size_t heap_used_pct = (used_heap_size / (float)heap_caps_get_total_size(MALLOC_CAP_DEFAULT)) * 100;
    StatusUpdateManager::_status_report.heap_used_pct = heap_used_pct;
    StatusUpdateManager::_status_report.ip_address = (char *)WiFiManager::ip_string().c_str();
    StatusUpdateManager::_status_report.temperature = StatusUpdateManager::_measured_average_temperature.get();

    // TODO: Load warnings
    // Send status update
    size_t packed_length = nspanel_status_report__get_packed_size(&StatusUpdateManager::_status_report);
    buffer.resize(packed_length);
    packed_data_size = nspanel_status_report__pack(&StatusUpdateManager::_status_report, buffer.data());
    xSemaphoreGive(StatusUpdateManager::_status_report_mutex);
  } else {
    ESP_LOGE("StatusUpdateManager", "Failed to get _status_update_mutex when trying to send status update!");
  }

  if (packed_data_size > 0) {
    // Build MQTT topic to publish status report on:
    std::string status_report_topic = "nspanel/";
    status_report_topic.append(WiFiManager::mac_string());
    status_report_topic.append("/status_report");

    if (MqttManager::publish(status_report_topic, (const char *)buffer.data(), packed_data_size, false) != ESP_OK) {
      ESP_LOGW("StatusUpdateManager", "Failed to send status report. Will try again next time.");
    }
  }
}

void StatusUpdateManager::_measure_temperature(void *arg) {
  // TODO: Implement logic to get actual temperature

  uint32_t read_voltage_mv;
  if (esp_adc_cal_get_voltage(adc_channel_t::ADC_CHANNEL_2, StatusUpdateManager::_adc_chars, &read_voltage_mv) == ESP_OK) {
    // We now have temperature as a voltage. Convert voltage into celsius:
    double read_voltage_v = (double)read_voltage_mv / 1000.0; // Convert mV to V.
    double current_temperature = 25 + log(((11200.0 * read_voltage_v / (3.3 - read_voltage_v)) / 10000)) + StatusUpdateManager::_temperature_offset_calibration;
    current_temperature = std::round(current_temperature * 10) / 10; // Round value to 1 decimal precision

    // If we have read the _measured_temperature_next_index slot previously, remove it from the total before setting the new value.
    if (StatusUpdateManager::_measured_temperature_total_samples >= StatusUpdateManager::_measured_temperature_next_index + 1) {
      StatusUpdateManager::_measured_temperature_total_sum -= StatusUpdateManager::_measured_temperatures[StatusUpdateManager::_measured_temperature_next_index]; // Remove old sample from current index from total
    }

    // We only have space for 30 samples.
    if (StatusUpdateManager::_measured_temperature_total_samples < 30) {
      StatusUpdateManager::_measured_temperature_total_samples++;
    }

    // Set _measured_temperature_next_index slot to read value and recalculate average temperature
    StatusUpdateManager::_measured_temperatures[StatusUpdateManager::_measured_temperature_next_index++] = current_temperature;
    StatusUpdateManager::_measured_temperature_total_sum += current_temperature;
    StatusUpdateManager::_measured_average_temperature.set(StatusUpdateManager::_measured_temperature_total_sum / StatusUpdateManager::_measured_temperature_total_samples);

    if (StatusUpdateManager::_measured_temperature_next_index >= 30) {
      StatusUpdateManager::_measured_temperature_next_index = 0;
    }
  } else {
    ESP_LOGW("StatusUpdateManager", "Failed to get read voltage while measuring temperature from NTC.");
  }
}

void StatusUpdateManager::_update_from_config() {
  NSPanelConfig config;
  if (NSPM_ConfigManager::get_config(&config) == ESP_OK) {
    StatusUpdateManager::_measure_temperature_in_fahrenheit = config.use_fahrenheit;
    StatusUpdateManager::_temperature_offset_calibration = config.temperature_calibration;
  } else {
    ESP_LOGE("StatusUpdateManager", "Failed to get config to determine if we should measure temperature in C or F. Will assume C.");
  }
}

void StatusUpdateManager::_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base == NSPM_CONFIGMANAGER_EVENT) {
    switch (event_id) {
    case nspm_configmanager_event::CONFIG_LOADED: {
      StatusUpdateManager::_update_from_config();
      break;
    }

    default:
      break;
    }
  }
}

void StatusUpdateManager::_update_manager_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  switch (event_id) {
  case updatemanager_event_t::FIRMWARE_UPDATE_STARTED: {
    ESP_LOGD("StatusUpdateManager", "Updating status report state.");
    if (xSemaphoreTake(StatusUpdateManager::_status_report_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      StatusUpdateManager::_status_report.nspanel_state = NSPanelStatusReport__State::NSPANEL_STATUS_REPORT__STATE__UPDATING_FIRMWARE;
      StatusUpdateManager::_status_report.update_progress = 0;
      xSemaphoreGive(StatusUpdateManager::_status_report_mutex);
    }

    // Restart timer with a 1 second interval instead of default 30
    esp_timer_stop(StatusUpdateManager::_status_update_timer);
    esp_timer_start_periodic(StatusUpdateManager::_status_update_timer, 1000 * 1000);
    break;
  }

  case updatemanager_event_t::LITTLEFS_UPDATE_STARTED: {
    if (xSemaphoreTake(StatusUpdateManager::_status_report_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      StatusUpdateManager::_status_report.nspanel_state = NSPanelStatusReport__State::NSPANEL_STATUS_REPORT__STATE__UPDATING_LITTLEFS;
      StatusUpdateManager::_status_report.update_progress = 0;
      xSemaphoreGive(StatusUpdateManager::_status_report_mutex);
    }

    // Restart timer with a 1 second interval instead of default 30
    esp_timer_stop(StatusUpdateManager::_status_update_timer);
    esp_timer_start_periodic(StatusUpdateManager::_status_update_timer, 1000 * 1000);
    break;
  }

  case updatemanager_event_t::NEXTION_UPDATE_STARTED: {
    if (xSemaphoreTake(StatusUpdateManager::_status_report_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      StatusUpdateManager::_status_report.nspanel_state = NSPanelStatusReport__State::NSPANEL_STATUS_REPORT__STATE__UPDATING_TFT;
      StatusUpdateManager::_status_report.update_progress = 0;
      xSemaphoreGive(StatusUpdateManager::_status_report_mutex);
    }

    // Restart timer with a 5 second interval instead of default 30. Use 5 seconds as the TFT/Nextion update is really slow.
    esp_timer_stop(StatusUpdateManager::_status_update_timer);
    esp_timer_start_periodic(StatusUpdateManager::_status_update_timer, 5000 * 1000);
    break;
  }

  case updatemanager_event_t::FIRMWARE_UPDATE_PROGRESS: {
    float *progress_percentage = (float *)event_data;
    if (xSemaphoreTake(StatusUpdateManager::_status_report_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
      StatusUpdateManager::_status_report.update_progress = std::round(*progress_percentage);
      xSemaphoreGive(StatusUpdateManager::_status_report_mutex);
    }
    break;
  }

  case updatemanager_event_t::LITTLEFS_UPDATE_PROGRESS: {
    float *progress_percentage = (float *)event_data;
    if (xSemaphoreTake(StatusUpdateManager::_status_report_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
      StatusUpdateManager::_status_report.update_progress = std::round(*progress_percentage);
      xSemaphoreGive(StatusUpdateManager::_status_report_mutex);
    }
    break;
  }

  case updatemanager_event_t::NEXTION_UPDATE_PROGRESS: {
    float *progress_percentage = (float *)event_data;
    if (xSemaphoreTake(StatusUpdateManager::_status_report_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
      StatusUpdateManager::_status_report.update_progress = std::round(*progress_percentage);
      xSemaphoreGive(StatusUpdateManager::_status_report_mutex);
    }
    break;
  }

  case updatemanager_event_t::FIRMWARE_UPDATE_FINISHED: {
    if (xSemaphoreTake(StatusUpdateManager::_status_report_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
      StatusUpdateManager::_status_report.update_progress = 100;
      xSemaphoreGive(StatusUpdateManager::_status_report_mutex);
    }
    break;
  }

  case updatemanager_event_t::LITTLEFS_UPDATE_FINISHED: {
    if (xSemaphoreTake(StatusUpdateManager::_status_report_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
      StatusUpdateManager::_status_report.update_progress = 100;
      xSemaphoreGive(StatusUpdateManager::_status_report_mutex);
    }
    break;
  }

  case updatemanager_event_t::NEXTION_UPDATE_FINISHED: {
    if (xSemaphoreTake(StatusUpdateManager::_status_report_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
      StatusUpdateManager::_status_report.update_progress = 100;
      xSemaphoreGive(StatusUpdateManager::_status_report_mutex);
    }
    break;
  }

  default:
    break;
  }
}