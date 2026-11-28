#pragma once

#include <atomic>
#include <cstdint>
#include <esp_event.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <memory>
#include <protobuf_nspanel.pb-c.h>
#include <protobuf_nspanel_entity.pb-c.h>
#include <string>

/*
 * The page that controls a single entity: a light (brightness, colour temperature or colour), a
 * thermostat (set temperature and its options) or a cover (open/stop/close, presets, position and
 * tilt). It follows the entity's state topic and draws the view for the entity's type. Entities
 * that can only be toggled, such as switches, have no page: the entities page toggles them.
 */
class EntityPage {
public:
  /**
   * Show the page for the entity whose state is published on the given topic.
   */
  static void show(std::string state_topic);

  /**
   * The page has been replaced by another page.
   */
  static void unshow();

  /**
   * The state of the entity shown, or nullptr until the first state has arrived. Keep the copy for
   * the whole operation, as for _get_current_state().
   */
  static std::shared_ptr<NSPanelEntityState> get_current_state();

  /**
   * The state topic of the entity shown.
   */
  static std::string get_current_state_topic();

  /**
   * Touch input from the page's widgets. The LVGL callbacks only queue these (they run on the LVGL
   * task with the LVGL lock held); _task_handle_input acts on them, as that publishes to MQTT and
   * switches pages.
   */
  struct InputEvent {
    enum Type : uint8_t {
      BACK,
      MODE,               // Header button: light colour temperature/colour, cover position/tilt
      SLIDER_SET,         // A slider was released. index: which Slider, value: its value
      THERMOSTAT_UP,      // Raise the set temperature, or the next value of the option being edited
      THERMOSTAT_DOWN,    // Lower the set temperature, or the previous value of the option being edited
      THERMOSTAT_OPTION,  // index: option tapped
      THERMOSTAT_SEND_SET_TEMPERATURE, // The set temperature has not changed for a moment: send it
      COVER_OPEN,
      COVER_STOP,
      COVER_CLOSE,
      COVER_PRESET, // index: preset 0-2
      STATE_RECEIVED, // A new state arrived over MQTT: redraw. Drawn here, not on the MQTT task, see _handle_mqtt_event
    } type;
    uint8_t index;
    int16_t value;
  };

  enum Slider : uint8_t {
    LIGHT_BRIGHTNESS,
    LIGHT_COLOR_TEMPERATURE_OR_SATURATION,
    LIGHT_HUE,
    COVER_POSITION, // Position or tilt, after the cover's edit mode
  };

  /**
   * Queue touch input for _task_handle_input. Never blocks; input is dropped if the queue is full.
   */
  static void queue_input(InputEvent event);

  /**
   * The icon pack changed: draw thermostat option icons with its font. Called with the LVGL lock
   * held, see IconPackManager::add_on_change().
   */
  static void refresh_icons();

  // What the light and cover views edit, switched with the header's mode button.
  enum class LightMode : uint8_t {
    COLOR_TEMPERATURE,
    COLOR,
  };
  enum class CoverMode : uint8_t {
    POSITION,
    TILT,
  };

private:
  /**
   * Handle events from MQTT, such as new state events:
   */
  static void _handle_mqtt_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * Draw the view for the current state. Takes the LVGL lock.
   */
  static void _update_display();

  /**
   * Act on queued touch input, see InputEvent.
   */
  static void _task_handle_input(void *param);
  static void _handle_light_input(const std::shared_ptr<NSPanelEntityState> &state, const InputEvent &event);
  static void _handle_thermostat_input(const std::shared_ptr<NSPanelEntityState> &state, const InputEvent &event);
  static void _handle_cover_input(const std::shared_ptr<NSPanelEntityState> &state, const InputEvent &event);

  /**
   * Go back to the entities page, sending a thermostat change that has not been sent yet.
   */
  static void _go_back();

  /**
   * Publish a command to the manager.
   */
  static void _send_command(NSPanelMQTTManagerCommand *command);

  static void _send_light_command(const NSPanelEntityState__Light *light, Slider slider, int32_t value);

  // Send the option being edited, or the set temperature if it was changed, and stop editing.
  static void _send_thermostat_option_command(const std::shared_ptr<NSPanelEntityState> &state);
  static void _send_thermostat_setpoint_command(const std::shared_ptr<NSPanelEntityState> &state);
  static void _set_temperature_timer_callback(void *arg);

  static void _send_cover_action(const NSPanelEntityState__Cover *cover, NSPanelMQTTManagerCommand__CoverCommand__Action action);
  static void _send_cover_position(const NSPanelEntityState__Cover *cover, bool tilt, int32_t position);

  /**
   * properly delete pointer and clear old data when shared_ptr expires
   */
  static void _delete_nspanel_entity_state_object(NSPanelEntityState *object);

  // Get a shared_ptr to the current state of the entity.
  static std::shared_ptr<NSPanelEntityState> _get_current_state();

  // Vars
  // The state topic of the given entity. Written by show() only; guarded by _current_state_mutex.
  static inline std::string _current_entity_mqtt_topic;
  static inline bool _mqtt_handler_registered = false;

  // Light: what the second slider edits. Taken from the light's state when its first state arrives.
  static inline std::atomic<LightMode> _light_mode = LightMode::COLOR_TEMPERATURE;
  // Cover: what the slider and buttons control.
  static inline std::atomic<CoverMode> _cover_mode = CoverMode::POSITION;
  // Set until the first state of the entity shown has arrived.
  static inline std::atomic<bool> _waiting_for_first_state = true;
  // A STATE_RECEIVED is in the input queue: states that arrive meanwhile need no other.
  static inline std::atomic<bool> _redraw_queued = false;

  // Thermostat. Only used by the input task, apart from the display, which reads them under the
  // LVGL lock after the input task changed them. Editing an option cycles through its values
  // locally; the value is sent when another option is tapped, the same option is tapped again, or
  // the page is left.
  static inline int8_t _editing_option = -1; // Index of the option being edited, -1 when none
  static inline int8_t _editing_value = -1;  // Index of the value selected for it
  // The set temperature asked for with up/down, valid while _set_temperature_changed. It is sent
  // SET_TEMPERATURE_SEND_DELAY after the last tap, and shown until the thermostat reports it.
  static inline float _requested_set_temperature = 0;
  static inline bool _set_temperature_changed = false;
  static inline esp_timer_handle_t _set_temperature_timer = NULL;

  // Cover: the last position asked for from this page, shown on the slider until the cover
  // reports that it reached it, or until the request times out.
  static inline std::atomic<int32_t> _pending_position = -1;
  static inline std::atomic<int64_t> _pending_position_requested_at = 0;
  static constexpr int64_t _pending_position_timeout_us = 5 * 1000 * 1000;

  // Written by the MQTT handler, read by the input task. Never dereference _current_state directly:
  // `->` on a shared_ptr returns a raw pointer and holds no reference, so a new state arriving mid-read can
  // free the one you are reading. Call _get_current_state() once per entry point, keep that copy for the
  // whole operation, and pass it down by const&. It returns nullptr until the first state has arrived.
  static inline SemaphoreHandle_t _current_state_mutex = NULL;
  static inline std::shared_ptr<NSPanelEntityState> _current_state;

  // Touch input waiting for _task_handle_input
  static inline QueueHandle_t _input_queue = NULL;
};
