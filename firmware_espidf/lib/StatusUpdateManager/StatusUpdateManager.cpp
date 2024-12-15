#include <ConfigManager.hpp>
#include <MqttManager.hpp>
#include <NSPM_ConfigManager.hpp>
#include <StatusUpdateManager.hpp>
#include <WiFiManager.hpp>
#include <cmath>
#include <driver/adc.h>
#include <esp_adc_cal.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wifi.h>

void StatusUpdateManager::init() {
  esp_log_level_set("StatusUpdateManager", esp_log_level_t::ESP_LOG_DEBUG); // TODO: Load from config

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

  NSPanelConfig config;
  if (NSPM_ConfigManager::get_config(&config) == ESP_OK) {
    StatusUpdateManager::_measure_temperature_in_fahrenheit = config.use_fahrenheit;
  } else {
    ESP_LOGE("StatusUpdateManager", "Failed to get config to determine if we should measure temperature in C or F. Will assume C.");
  }

  // Set default status report state
  // TODO: Implement UpdateManager events to update nspanel_state and also update_progress
  nspanel_status_report__init(&StatusUpdateManager::_status_report);
  StatusUpdateManager::_status_report.nspanel_state = NSPanelStatusReport__State::NSPANEL_STATUS_REPORT__STATE__ONLINE;
  StatusUpdateManager::_status_report.update_progress = 0;
  StatusUpdateManager::_status_report.rssi = 0;
  StatusUpdateManager::_status_report.temperature = 0; // TODO: Implement logic to get actual temperature

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

  size_t used_heap_size = heap_caps_get_total_size(MALLOC_CAP_DEFAULT) - xPortGetFreeHeapSize();
  size_t heap_used_pct = (used_heap_size / (float)heap_caps_get_total_size(MALLOC_CAP_DEFAULT)) * 100;
  StatusUpdateManager::_status_report.heap_used_pct = heap_used_pct;
  std::string ip_string = WiFiManager::ip_string();
  StatusUpdateManager::_status_report.ip_address = (char *)ip_string.c_str();
  std::string mac_string = WiFiManager::mac_string();
  StatusUpdateManager::_status_report.mac_address = (char *)mac_string.c_str();
  StatusUpdateManager::_status_report.temperature = StatusUpdateManager::_measured_average_temperature.get();

  // TODO: Load warnings

  // Build MQTT topic to publish status report on:
  std::string status_report_topic = "nspanel/mqttmanager_";
  status_report_topic.append(NSPM_ConfigManager::get_manager_address());
  status_report_topic.append("/nspanel/");
  status_report_topic.append(ConfigManager::wifi_hostname);
  status_report_topic.append("/status_report");

  // Send status update
  uint32_t packed_length = nspanel_status_report__get_packed_size(&StatusUpdateManager::_status_report);
  std::vector<uint8_t> buffer(packed_length); // Use vector for automatic cleanup of data when going out of scope
  size_t packed_data_size = nspanel_status_report__pack(&StatusUpdateManager::_status_report, buffer.data());
  if (MqttManager::publish(status_report_topic, (const char *)buffer.data(), packed_data_size, false) != ESP_OK) {
    ESP_LOGW("StatusUpdateManager", "Failed to send status report. Will try again next time.");
  }
}

void StatusUpdateManager::_measure_temperature(void *arg) {
  // TODO: Implement logic to get actual temperature

  uint32_t read_voltage;
  if (esp_adc_cal_get_voltage(adc_channel_t::ADC_CHANNEL_2, StatusUpdateManager::_adc_chars, &read_voltage) == ESP_OK) {
    // We now have temperature as a voltage. Convert voltage into celsius:
    double current_temperature = (double)read_voltage / 1000.0; // Convert mV to V.
    current_temperature = 25 + log(((11200.0 * current_temperature / (3.3 - current_temperature)) / 10000));

    // Store the last 30 measurements
    if (StatusUpdateManager::_measured_temperature_total_samples >= StatusUpdateManager::_measured_temperature_next_index + 1) {
      StatusUpdateManager::_measured_temperature_total_sum -= StatusUpdateManager::_measured_temperatures[StatusUpdateManager::_measured_temperature_next_index]; // Remove old sample from current index from total
    }

    if (StatusUpdateManager::_measured_temperature_total_samples < 30) {
      StatusUpdateManager::_measured_temperature_total_samples++;
    }

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