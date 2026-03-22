#pragma once
#include <MutexWrapper.hpp>
#include <esp_event.h>
#include <functional>
#include <memory>
#include <protobuf_nspanel.pb-c.h>

class InterfaceManager {
public:
  /**
   * @brief Initialize the InterfaceManager and the Nextion display. Start loading config and enter main interface.
   */
  static void init();

  /**
   * When calling "show" on any page this can be used to unshow the previous page if
   * that page had declared an unshow callback function.
   */
  static inline MutexWrapped<std::function<void()>> current_page_unshow_callback;

  /**
   * Start a task to call unshow on given callback. This is to not remove a registered loop event handler
   * from within that handler. Doing so will cause a crash.
   */
  static void call_unshow_callback();

  /**
   * Show the user selected default page
   */
  static void show_default_page();

private:
  /**
   * @brief Handle any event trigger from the Nextion display
   */
  static void _nextion_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * @brief Handle any event trigger from the UpdateManager
   */
  static void _update_manager_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * @brief Handle any event trigger from the NSPM_ConfigManager
   */
  static void _nspm_configmanager_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * @brief Handle any event trigger from the RoomManager
   */
  static void _room_manager_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * @brief Handle data received over MQTT and apply new interface manager settings if applicable.
   */
  static void _mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /*
   * @brief Subscribe to relevant MQTT topics for InterfaceManager
   */
  static void _subscribe_to_relevant_mqtt_topics();

  /**
   * Unshow the currently showing page by starting a task to prevent the running task from removing
   * its own task handler from within. Doing so will cause a crash
   */
  static void _task_unshow_page(void *param);

  // Vars
  // Queue of unshow function pointer to call from _task_unshow_page
  static inline QueueHandle_t _unshow_queue;

  // In the case of a Nextion "sleep" event, ie. trigger screensaver, should we
  // or is the screensaver blocked?
  static inline MutexWrapped<bool> _screensaver_blocked = false;

  // Has the config been loaded from NSPanel Manager container yet?
  static inline bool _nspm_config_loaded = false;

  // Has the status for the home page been loaded from the manager yet?
  static inline bool _home_page_status_loaded = false;

  // Has the panel been accepted by a manager as of yet?
  static inline bool _has_been_associated_with_manager = false;

  // Current NSPanelConfig. Primarily used to check if current screensaver timeout has changed and if so update the interval.
  static inline std::shared_ptr<NSPanelConfig> _nspm_cur_config;

  // MQTT Topics
  static inline std::string _screen_on_off_command_topic;
  static inline std::string _screen_on_off_state_topic;
  static inline std::string _screen_brightness_command_topic;
  static inline std::string _screen_brightness_state_topic;
  static inline std::string _screensaver_brightness_command_topic;
  static inline std::string _screensaver_brightness_state_topic;
  static inline std::string _screensaver_mode_command_topic;
  static inline std::string _screensaver_mode_state_topic;
  static inline std::string _screen_raw_commands;
};