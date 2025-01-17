#pragma once
#include <MutexWrapper.hpp>
#include <atomic>
#include <esp_event.h>
#include <protobuf_nspanel.pb-c.h>

class ScreensaverPage {
public:
  /**
   * Show the screensaver page
   */
  static void show();

  /**
   * Unshow the screensaver page
   */
  static void unshow();

private:
  /**
   * Update the display with new values for time and AM/PM
   */
  static void _update_displayed_time();

  /**
   * Update the display with new date
   */
  static void _update_displayed_date();

  /**
   * Update the display with new weather forecast and current data
   */
  static void _task_update_displayed_weather_data(void *param);

  /**
   * Set the relevant screensaver brightness
   */
  static void _update_display_brightness();

  /**
   * Go to the correct Nextion page and show/hide the background depending on user settings.
   */
  static void _go_to_nextion_page();

  /**
   * Handle events from MQTT manager, such as new weather forecasts sent over MQTT
   */
  static void _mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * Handle events from NSPM_ConfigManager
   */
  static void _nspm_config_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * Handle events from StatusUpdateManager for when a new average temperature has been calculated
   */
  static void _new_temperature_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * Used by std::shared_ptr to delete protobuf object once pointer goes out of scope
   */
  static void _shared_ptr_weather_update_cleanup(NSPanelWeatherUpdate *data);

  // Vars:
  // The most current weather data for forecast and current weather
  static inline std::shared_ptr<NSPanelWeatherUpdate> _weather_update_data = nullptr;
  static inline std::vector<uint8_t> _weather_update_mqtt_data; // Raw data received from MQTT

  // Mutex to only allow one task at the time access to _weather_update_data
  static inline SemaphoreHandle_t _weather_update_data_mutex = NULL;

  // The current time string
  static inline MutexWrapped<std::string> _current_time;

  // The current date string
  static inline MutexWrapped<std::string> _current_date;

  // Are we showing AM, PM or nothing as a string
  static inline MutexWrapped<std::string> _am_pm_string;

  // What brightness show the screensaver show. Default to 50%
  static inline std::atomic<uint8_t> _screensaver_brightness;

  // Is the screensaver page currently show?
  static inline std::atomic<bool> _currently_shown;

  // The temperature currently being shown on the screensaver
  static inline std::atomic<double> _current_temperature;

  // What screensaver mode is currently active.
  static inline MutexWrapped<NSPanelConfig__NSPanelScreensaverMode> _current_screensaver_mode;
};