#pragma once
#include <MutexWrapper.hpp>
#include <esp_adc_cal.h>
#include <esp_timer.h>
#include <protobuf_nspanel.pb-c.h>
#include <vector>

class StatusUpdateManager {
public:
  /**
   * Initialize the StatusUpdateManager to setup logging and start timers
   */
  static void init();

private:
  /**
   * Create a NSPanelStatusReport protobuf object and fill in all data. Then send it out over MQTT.
   */
  static void _send_status_update(void *arg);

  /**
   * Measure the temperature periodically to create a moving average
   */
  static void _measure_temperature(void *arg);

  // Vars:
  // Handle to timer responsible for sending status updates periodically
  static inline esp_timer_handle_t _status_update_timer;

  // Should temperature be measured in fahrenheit or celsius?
  static inline bool _measure_temperature_in_fahrenheit = false;

  // Handle to timer responsible for measuring temperature periodically
  static inline esp_timer_handle_t _measure_temperature_timer;

  // What is the current average measured temperature
  static inline MutexWrapped<double> _measured_average_temperature = 0;

  // Records of measured temperatures from _measure_temperature function.
  static inline float _measured_temperatures[30] = {0};

  // What index is the next index to insert/replace read temperature into.
  static inline uint8_t _measured_temperature_next_index;

  // How many samples has been read from the temperature sensor
  static inline uint8_t _measured_temperature_total_samples;

  // Total sum of all measured temperatures. Used to calculate _measured_average_temperature
  static inline float _measured_temperature_total_sum;

  // Status report object used to send protobuf data to manager
  static inline NSPanelStatusReport _status_report;

  // ADC characteristics
  static inline esp_adc_cal_characteristics_t *_adc_chars;

  // Start arguments for timer responsible for sending status updates
  static inline constexpr esp_timer_create_args_t _status_update_timer_args = {
      .callback = StatusUpdateManager::_send_status_update,
      .name = "status_update_timer",
  };

  // Start arguments for timer responsible for measuring temperature
  static inline constexpr esp_timer_create_args_t _measure_temperature_timer_args = {
      .callback = StatusUpdateManager::_measure_temperature,
      .name = "temperature_timer",
  };
};