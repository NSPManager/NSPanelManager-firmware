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
};