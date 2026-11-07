#pragma once
#include <array>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>

/**
 * The event handlers one page registered during one show().
 *
 * InterfaceManager::begin_page() hands each show() a fresh PageHandlers. add()
 * and add_to() only record the handler here: nothing is registered with
 * esp_event. InterfaceManager registers one dispatcher per event source once,
 * at boot, and each dispatcher calls the matching handlers of whichever page
 * is current when the event is delivered. Showing or hiding a page therefore
 * never registers or unregisters anything with esp_event.
 *
 * That matters because unregistering is not safe while a page is running.
 * When the loop is busy dispatching, esp_event_handler_instance_unregister()
 * holds the loop's mutex while it posts a cleanup event to the loop's queue,
 * waiting forever. If that queue is full, the loop task, which needs the same
 * mutex to dispatch the next event and drain the queue, waits forever too. The
 * default event loop then never dispatches again (seen on a panel whose queue
 * was filled by HTTP server and Wi-Fi events during a page switch).
 *
 * Only the current page receives an event, so a touch that switches page
 * part-way through its dispatch is never handled by the new page as well.
 */
class PageHandlers {
public:
  /**
   * @brief Call a handler for events on the default event loop for as long as this page is shown.
   * @return ESP_ERR_NOT_SUPPORTED if InterfaceManager has no dispatcher for this event base.
   */
  esp_err_t add(esp_event_base_t event_base, int32_t event_id, esp_event_handler_t event_handler, void *event_handler_arg = NULL);

  /**
   * @brief Call a handler for events on another event loop (e.g. RoomManager::event_loop()) for as long as this page is shown.
   * @return ESP_ERR_NOT_SUPPORTED if InterfaceManager has no dispatcher for this loop and event base.
   */
  esp_err_t add_to(esp_event_loop_handle_t event_loop, esp_event_base_t event_base, int32_t event_id, esp_event_handler_t event_handler, void *event_handler_arg = NULL);

  /**
   * @brief Stop accepting handlers. Anything added after this is ignored.
   */
  void retire();

  /**
   * @brief Call every handler added for this loop (NULL for the default loop), base and id.
   * Called by InterfaceManager's dispatchers, on the loop's own task.
   */
  void dispatch(esp_event_loop_handle_t event_loop, esp_event_base_t event_base, int32_t event_id, void *event_data);

private:
  struct Registration {
    esp_event_loop_handle_t event_loop; // NULL for the default loop
    esp_event_base_t event_base;
    int32_t event_id;
    esp_event_handler_t handler;
    void *arg;
  };

  // Pages register a handful of handlers; a fixed array keeps allocation out of the critical section.
  static constexpr size_t MAX_REGISTRATIONS = 8;
  std::array<Registration, MAX_REGISTRATIONS> _registrations = {};
  size_t _count = 0;
  bool _retired = false;
  portMUX_TYPE _lock = portMUX_INITIALIZER_UNLOCKED;
};
