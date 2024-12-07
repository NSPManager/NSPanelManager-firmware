#pragma once
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
  static void _update_displayed_weather_data();

  /**
   * Set the relevant screensaver brightness
   */
  static void _update_display_brightness();

  /**
   * Handle events from MQTT manager, such as new weather forecasts sent over MQTT
   */
  static void _mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  // Vars:
  // The most current weather data for forecast and current weather
  static inline NSPanelWeatherUpdate *_weather_update_data = NULL;

  // Mutex to only allow one task at the time access to _weather_update_data
  static inline SemaphoreHandle_t _weather_update_data_mutex = NULL;

  // The current time string
  static inline std::string _current_time;

  // The current date string
  static inline std::string _current_date;

  // Are we showing AM, PM or nothing as a string
  static inline std::string _am_pm_string;

  // What brightness show the screensaver show. Default to 50%
  static inline uint8_t _screensaver_brightness = 50;

  // Is the screensaver page currently show?
  static inline bool _currently_shown = false;

  // What screensaver mode is currently active.
  static inline NSPanelConfig__NSPanelScreensaverMode _current_screensaver_mode;

  // What screensaver mode was previously active.
  static inline NSPanelConfig__NSPanelScreensaverMode _last_current_screensaver_mode;

  // Has the screensaver page been show earlier? This is to only subscribe to MQTT topics once.
  static inline bool _previously_shown = false;
};