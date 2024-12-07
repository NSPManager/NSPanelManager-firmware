#pragma once
#include <esp_event.h>

class InterfaceManager {
public:
  /**
   * @brief Initialize the InterfaceManager and the Nextion display. Start loading config and enter main interface.
   */
  static void init();

  /**
   * ENUM representation of all available pages
   */
  enum available_pages {
    FIRST_PAGE_IGNORE, // This page does nothing, it's more of a flag that "Hey, this value is not set!"
    LOADING,
    HOME,
    SCENES,
    ENTITIES,
    SCREENSAVER,
  };

  static inline available_pages _current_page;

private:
  /**
   * @brief Handle any event trigger from the Nextion display
   */
  static void _nextion_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * @brief Handle any event trigger from the RoomManager
   */
  static void _room_manager_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
};