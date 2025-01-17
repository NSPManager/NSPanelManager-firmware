#pragma once
#include <MutexWrapper.hpp>
#include <esp_event.h>
#include <functional>

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
};