#pragma once
#include <MutexWrapper.hpp>
#include <driver/i2c.h>
#include <driver/i2c_master.h>
#include <esp_adc_cal.h>
#include <esp_timer.h>
#include <protobuf_nspanel.pb-c.h>
#include <vector>

#if defined(BOARD_CUSTOM)
#include <bmx280.h>
#endif

class StatusUpdateManager {
public:
  /**
   * Initialize the StatusUpdateManager to setup logging and start timers
   */
  static void init();

  // Get the currently calculated average temperature
  static float current_temperature();

  // Send the reboot state over MQTT and then reboot.
  static void reboot();

private:
  /**
   * Create a NSPanelStatusReport protobuf object and fill in all data. Then send it out over MQTT.
   */
  static void _send_status_update(void *arg);

  /**
   * Measure the temperature periodically to create a moving average
   */
  static void _measure_temperature(void *arg);

  /**
   * Update internal values from loaded config
   */
  static void _update_from_config();

  /**
   * Handle events such as new config loaded
   */
  static void _event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * Handle firmware/littlefs/nextion update events
   */
  static void _update_manager_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

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
  static inline float _measured_temperatures[30];

  // What index is the next index to insert/replace read temperature into.
  static inline uint8_t _measured_temperature_next_index;

  // How many samples has been read from the temperature sensor
  static inline uint8_t _measured_temperature_total_samples;

  // Calibration value for the read temperature from the manager
  static inline float _temperature_offset_calibration;

#if defined(BOARD_CUSTOM)
  // What is the current average measured temperature
  static inline MutexWrapped<double> _measured_average_humidity = 0;

  // Records of measured humidity from _measure_temperature function.
  static inline float _measured_humidity[30];

  // What index is the next index to insert/replace read humidity into.
  static inline uint8_t _measured_humidity_next_index;

  // How many samples has been read from the pressure sensor
  static inline uint8_t _measured_humidity_total_samples;

  // What is the current average measured temperature
  static inline MutexWrapped<double> _measured_average_pressure = 0;

  // Records of measured pressure from _measure_temperature function.
  static inline float _measured_pressure[30];

  // What index is the next index to insert/replace read pressure into.
  static inline uint8_t _measured_pressure_next_index;

  // How many samples has been read from the humidity sensor
  static inline uint8_t _measured_pressure_total_samples;

#endif

  // Mutex to only allow once task at the time access to the _status_report.
  static inline SemaphoreHandle_t _status_report_mutex;

  // Status report object used to send protobuf data to manager
  static inline NSPanelStatusReport _status_report;

  // ADC characteristics
  static inline esp_adc_cal_characteristics_t *_adc_chars;

#if defined(BOARD_CUSTOM)
  // Custom PCB that uses I2C sensors. Define them:

  static bool _initialize_bme280();

  // I2C Master bus handle
  static inline i2c_master_bus_handle_t _i2c_master_bus_handle;

  // Was the BME280 sensor initialized correctly
  static inline bool _bme280_initialized = false;

  // Time of last try to initialize the BME280 sensor
  static inline uint64_t _last_bme280_init_try = 0;

  // Device handle for BME280 sensor on I2C bus.
  static inline bmx280_t *_bme280_dev_handle = NULL;

  // static bool _initialize_ltr303();

  // // Was the LTR303 sensor initialized correctly
  // static inline bool _ltr303_initialized = false;

  // // Time of last try to initialize the LTR303 sensor
  // static inline uint64_t _last_ltr303_init_try = 0;

  // // Device handle for LTR-303ALS-01 sensor on I2C bus.
  // static inline LTR303 *_ltr303_dev_handle = NULL;
#endif

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