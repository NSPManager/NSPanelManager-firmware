#pragma once
#include <MutexWrapper.hpp>
#include <atomic>
#include <esp_event.h>
#include <lvgl.h>
#include <protobuf_nspanel.pb-c.h>

class ScreensaverPage {
public:
  /**
   * Initialize and start listening for events and update data on screensaver page in background so that
   * the screensaver is ready when it's time to show it.
   */
  static void init();

  /**
   * Show the screensaver page
   */
  static void show();

  /**
   * Unshow the screensaver page
   */
  static void unshow();

  /**
   * Is the screensaver page currently being shown?
   */
  static bool showing();

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
   * Update the display with new temperature
   */
  static void _update_displayed_temperature();

  /**
   * Update the display with new weather forecast and current data
   */
  static void _task_update_displayed_weather_data(void *param);

  /**
   * Set the relevant screensaver brightness
   */
  static void _update_display_brightness();

  /**
   * Show the screen for the configured screensaver mode, with the optional parts the user has enabled, and set its brightness.
   */
  static void _show_screen_for_mode();

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

  /**
   * Subscribe to relevant MQTT topics, moving existing subscriptions if the manager address or inside temperature topic changed.
   */
  static void _subscribe_to_mqtt_topics();

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

  // The current temperature that is to be displayed
  static inline MutexWrapped<std::string> _current_temperature;

  // What the MQTT handler received and has not been drawn yet, CHANGED_* bits. The handler never
  // draws: esp-mqtt runs it holding its API lock, and waiting there for the LVGL lock would stop
  // every publish on the panel meanwhile. _draw_changes draws them, on an LVGL timer.
  static constexpr uint8_t CHANGED_TIME = 1, CHANGED_DATE = 2, CHANGED_TEMPERATURE = 4;
  static inline std::atomic<uint8_t> _changed = 0;
  static inline lv_timer_t *_draw_changes_timer = nullptr; // Guarded by the LVGL lock
  static void _draw_changes(lv_timer_t *timer);

  // MQTT state topic for any configured inside temperature sensor. In case of empty, use internal NTC thermistor.
  static inline MutexWrapped<std::string> _inside_temperature_sensor_state_topic;

  // Manager address the status topics were last subscribed for.
  static inline MutexWrapped<std::string> _subscribed_manager_address;

  // What brightness show the screensaver show. Default to 50%
  static inline std::atomic<uint8_t> _screensaver_brightness;

  // What screensaver mode is currently active.
  static inline MutexWrapped<NSPanelConfig__NSPanelScreensaverMode> _current_screensaver_mode;
};