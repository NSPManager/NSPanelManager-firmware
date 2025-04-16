#pragma once

#include <atomic>
#include <memory>
#include <protobuf_nspanel_entity.pb-c.h>
#include <string>

/*
 * The page that will handle control of individual entities
 */
class EntityPage {
public:
  /**
   * Show the loading page
   */
  static void show(std::string state_topic);

  /**
   * Unshow the loading page
   */
  static void unshow();

private:
  /**
   * Handle events from MQTT, such as new state events:
   */
  static void _handle_mqtt_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * Handle events from Nextion, such as touch events:
   */
  static void _handle_nextion_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * Handle event for a new config from MQTTManager:
   */
  static void _handle_config_update(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * Delegate to the correct update method
   */
  static void _update_display();

  /**
   * Delegate to the correct touch method
   */
  static void _handle_touch_event(uint16_t component_id, bool pressed);

  /*
   * Update displayed page and value of "Light" page.
   */
  static void _update_display_light();

  /*
   * Handle touch event for "Light" page.
   */
  static void _handle_touch_event_light(uint16_t component_id, bool pressed);

  /**
   * properly delete pointer and clear old data when shared_ptr expires
   */
  static void _delete_nspanel_entity_state_object(NSPanelEntityState *object);

  // What is the page currently showing
  enum _entity_page_modes {
    LIGHT_COLOR_TEMPERATURE,
    LIGHT_RGB,
  };
  static inline std::atomic<_entity_page_modes> _current_mode;

  // Vars
  // The state topic of the given entity
  static inline std::string _current_entity_mqtt_topic;

  // Are we currently displaying the page?
  static inline std::atomic<bool> _currently_showing = false;

  // The last name to be updated on the display.
  static inline std::string _last_displayed_name = "";

  // Light specific variables
  static inline uint8_t _last_brightness = 0;
  static inline uint8_t _last_kelvin_pct = 0;
  static inline uint8_t _last_hue = 0;
  static inline uint8_t _last_saturation_pct = 0;

  static inline SemaphoreHandle_t _current_state_mutex = NULL;
  static inline std::shared_ptr<NSPanelEntityState> _current_state;
};