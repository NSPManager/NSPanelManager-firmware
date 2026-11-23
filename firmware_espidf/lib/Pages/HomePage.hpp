#pragma once
#include <MutexWrapper.hpp>
#include <atomic>
#include <cstdint>
#include <esp_event.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <memory>
#include <optional>
#include <protobuf_nspanel.pb-c.h>
#include <string>

class HomePage {
public:
  static void show();
  static void unshow();

  /**
   * Available "Affect" modes
   */
  enum HomePageAffectMode {
    ROOM,
    ALL
  };

  /**
   * Set the current affect mode for home page.
   */
  static void set_current_affect_mode(HomePageAffectMode mode);

  /**
   * Available "edit" modes
   */
  enum HomePageEditMode {
    ALL_LIGHTS,
    CEILING_LIGHTS,
    TABLE_LIGHTS,
  };

  /**
   * Set the current edit mode for home page.
   */
  static void set_current_edit_mode(HomePageEditMode mode);

  /**
   * Touch input from the page's widgets. The LVGL callbacks only queue these
   * (they run on the LVGL task with the LVGL lock held); _task_handle_input
   * acts on them, as that may publish to MQTT and update RoomManager.
   */
  struct InputEvent {
    enum Type : uint8_t {
      CEILING_BUTTON_PRESSED,
      CEILING_BUTTON_RELEASED,
      TABLE_BUTTON_PRESSED,
      TABLE_BUTTON_RELEASED,
      BRIGHTNESS_SLIDER_SET,
      COLOR_TEMPERATURE_SLIDER_SET,
      NEXT_ROOM,
      NEXT_MODE,
      SHOW_SCENES,        // The room's scenes, or the global scenes when showing all rooms
      SHOW_ROOM_ENTITIES, // The room's entities, only when showing a single room
    } type;
    int32_t value; // Slider value for the *_SLIDER_SET events
  };

  /**
   * Queue touch input for _task_handle_input. Never blocks; input is dropped if the queue is full.
   */
  static void queue_input(InputEvent event);

private:
  /**
   * Update displayed values, modes and components given the current room status
   */
  static void _update_display();

  /**
   * Handle events from RoomManager, such as new room data arrived, update the display.
   */
  static void _handle_roommanager_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * Handle event for a new config from MQTTManager:
   */
  static void _handle_config_update(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * Act on queued touch input, see InputEvent.
   */
  static void _task_handle_input(void *param);

  /**
   * Toggle between showing the current room and all rooms, if the panel may control other rooms.
   */
  static void _go_to_next_mode();

  /**
   * Handle "Master ceiling lights" button events
   * @param pressed: Is this a touch down or touch released event
   */
  static void _handle_master_ceiling_lights_button(bool pressed);

  /**
   * Handle "Master table lights" button events
   * @param pressed: Is this a touch down or touch released event
   */
  static void _handle_master_table_lights_button(bool pressed);

  /**
   * Handle touch event on "Ceiling master button"
   */
  static void _send_ceiling_master_button_command_to_manager();

  /**
   * Handle touch event on "Table master button"
   */
  static void _send_table_master_button_command_to_manager();

  /**
   * Handle brightness slider touch event
   */
  static void _handle_brightness_slider_event();

  /**
   * Handle color temperature slider touch event
   */
  static void _handle_color_temperature_slider_event();

  /**
   * Set the cached value for the "Brightness"-slider from a value the user selected on it
   */
  static void _update_brightness_slider_cache(int32_t value);

  /**
   * Set the cached value for the "Color temperature"-slider from a value the user selected on it
   */
  static void _update_color_temperature_slider_cache(int32_t value);

  /**
   * Function that will be called once the "Special mode" (ie. hold on table or ceiling lights buttons) timer triggers
   */
  static void _special_mode_timer_callback(void *arg);

  /**
   * Function that will be called once the "Special mode timeout" (ie. hold on table or ceiling lights buttons) timeout timer triggers
   */
  static void _special_mode_timeout_timer_callback(void *arg);

  // When pressing something, what are we supposed to affect?
  static inline HomePageAffectMode _current_affect_mode = HomePageAffectMode::ROOM;

  // What mode is the home page in?
  static inline HomePageEditMode _current_edit_mode = HomePageEditMode::ALL_LIGHTS;

  // The cached value for the "Brightness"-slider
  static inline std::atomic<int32_t> _cache_brightness_slider;

  // The cached value for the "Color temperature"-slider
  static inline std::atomic<int32_t> _cache_color_temperature_slider;

  // The cached value for the "Ceiling lights" brightness label
  static inline std::atomic<int32_t> _cache_ceiling_light_brightness;

  // The cached value for the "Table lights" brightness label
  static inline std::atomic<int32_t> _cache_table_light_brightness;

  // Should we ignore the next release of the ceiling button? This is used to
  // ignore the touch up that happens after special mode is activated
  static inline uint64_t _ignore_next_ceiling_button_touch_up = false;

  // Should we ignore the next release of the table button? This is used to
  // ignore the touch up that happens after special mode is activated
  static inline uint64_t _ignore_next_table_button_touch_up = false;

  // What mode shall be activated once the "Special mode" (ie. hold on table or ceiling lights buttons) timer triggers
  static inline HomePageEditMode _special_mode_timer_activation_mode;

  // Arguments that are used to create the special mode timer
  static inline esp_timer_create_args_t _special_mode_create_timer_args;

  // Arguments that are used to create the special mode timeout timer
  static inline esp_timer_create_args_t _special_mode_timeout_create_timer_args;

  // The actual handle to access the ESP timer that handles special mode activation
  static inline esp_timer_handle_t _special_mode_timer_handle = NULL;

  // The actual handle to access the ESP timer that handles special mode deactivation
  static inline esp_timer_handle_t _special_mode_timeout_timer_handle = NULL;

  // Touch input waiting for _task_handle_input
  static inline QueueHandle_t _input_queue = NULL;

  // The default room from the last config this page saw, used to check if the "default room" has changed when a new config is received.
  // Written from whichever task calls show() (default loop, MQTT task, RoomManager loop) and from the config handler on the default
  // loop, so it must be guarded. It used to hold the whole config as a shared_ptr, which raced when one task reassigned it while
  // another was dereferencing it.
  static inline MutexWrapped<std::optional<int32_t>> _last_default_room;
};
