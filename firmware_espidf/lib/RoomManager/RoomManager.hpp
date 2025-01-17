#pragma once
#include <esp_event.h>
#include <list>
#include <memory>
#include <protobuf_nspanel.pb-c.h>

class RoomManager {
public:
  /**
   * Load and initialize RoomManager.
   */
  static void init();

  /**
   * @brief Get the home page status
   * @param status: Reference to where to save NSPanelRoomStatus to
   * @return ESP_OK if operation was successful, otherwise ESP_ERR_NOT_FINISHED
   */
  static esp_err_t get_home_page_status(std::shared_ptr<NSPanelRoomStatus> *status);

  /**
   * @brief Get the home page status for all rooms
   * @param status: Reference to where to save NSPanelRoomStatus to
   * @return ESP_OK if operation was successful, otherwise ESP_ERR_NOT_FINISHED
   */
  static esp_err_t get_home_page_status_all_rooms(std::shared_ptr<NSPanelRoomStatus> *status);

  /**
   * @brief Get a room status pointer to a __unpacked protobuf
   * @param status: Reference to where to save NSPanelRoomStatus to
   * @return ESP_OK if operation was successful, otherwise ESP_ERR_NOT_FINISHED
   */
  static esp_err_t get_home_page_status_mutable(std::shared_ptr<NSPanelRoomStatus> *status);

  /**
   * @brief Get a room status pointer to a __unpacked protobuf for status for all rooms
   * @param status: Reference to where to save NSPanelRoomStatus to
   * @return ESP_OK if operation was successful, otherwise ESP_ERR_NOT_FINISHED
   */
  static esp_err_t get_home_page_status_mutable_all_rooms(std::shared_ptr<NSPanelRoomStatus> *status);

  /**
   * @brief Navigate to the previous room in order
   * @return ESP_OK if operation was successful, otherwise ESP_ERR_NOT_FINISHED or ESP_ERR_NOT_FOUND
   */
  static esp_err_t go_to_previous_room();

  /**
   * @brief Navigate to the next room in order
   * @return ESP_OK if operation was successful, otherwise ESP_ERR_NOT_FINISHED or ESP_ERR_NOT_FOUND
   */
  static esp_err_t go_to_next_room();

  /**
   * @brief Will replace current room status with the one provided
   * @param status: The new room status to be replaced into the list of rooms.
   * @return ESP_OK if operation was successful, otherwise ESP_ERR_NOT_FINISHED
   */
  static esp_err_t replace_home_page_status(std::shared_ptr<NSPanelRoomStatus> status);

  /**
   * @brief Will replace room status object respresenting all rooms status with the one provided
   * @param status: The new room status to be replaced into the list of rooms.
   * @return ESP_OK if operation was successful, otherwise ESP_ERR_NOT_FINISHED
   */
  static esp_err_t replace_home_page_status_all_rooms(std::shared_ptr<NSPanelRoomStatus> status);

  /**
   * @brief Get a NSPanelRoomEntitiesPage status object for the currently selected room that contains all entities to be displayed and their values
   * @param status: Reference to where to save NSPanelRoomEntitiesPage to
   * @return ESP_OK if operation was successful, otherwise ESP_ERR_NOT_FINISHED
   */
  static esp_err_t get_current_room_entities_page_status(std::shared_ptr<NSPanelRoomEntitiesPage> *status);

  /**
   * @brief Navigate to the previous entities page in order
   * @return ESP_OK if operation was successful, otherwise ESP_ERR_NOT_FINISHED or ESP_ERR_NOT_FOUND
   */
  static esp_err_t go_to_previous_entities_page();

  /**
   * @brief Navigate to the next entities page in order
   * @return ESP_OK if operation was successful, otherwise ESP_ERR_NOT_FINISHED or ESP_ERR_NOT_FOUND
   */
  static esp_err_t go_to_next_entities_page();

  /**
   * @brief Register event handler for events.
   * @param event_id: The event id to subscribe to.
   * @param event_handler: The event handler callback to register
   * @param event_handler_arg: Any argument to the event handler.
   */
  static esp_err_t register_handler(int32_t event_id, esp_event_handler_t event_handler, void *event_handler_arg);

  /**
   * @brief Unregister event handler for events.
   * @param event_id: The event id to unsubscribe from.
   * @param event_handler: The event handler callback to register
   */
  static esp_err_t unregister_handler(int32_t event_id, esp_event_handler_t event_handler);

private:
  /**
   * @brief Handle events triggered from MQTT places.
   */
  static void _mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * @brief Handle events triggered from MQTT when it is connected to MQTT.
   */
  static void _mqtt_event_handler_connected(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * @brief Handle events triggered from other places.
   */
  static void _event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * Function used when shared_ptr goes out of scope. Used to delete the underlying object
   * using the nspanel_room_status__free_unpacked function
   */
  static void _nspanel_room_status_shared_ptr_deleter(NSPanelRoomStatus *status);

  /**
   * Function used when shared_ptr goes out of scope. Used to delete the underlying object
   * using the nspanel_room_entities_page__free_unpacked function
   */
  static void _nspanel_room_entities_page_shared_ptr_deleter(NSPanelRoomEntitiesPage *status);

  // Vars:
  // Status of home page for currently selected room
  static inline std::shared_ptr<NSPanelRoomStatus> _home_page;

  // STatus of home page for all rooms
  static inline std::shared_ptr<NSPanelRoomStatus> _home_page_all_rooms;

  // Mutex to only allow one task at the time to access the status of the home page
  static inline SemaphoreHandle_t _home_page_mutex;

  // Current entities page that is showing
  static inline std::shared_ptr<NSPanelRoomEntitiesPage> _entities_page;

  // Mutex to allow only one task at the time to access the current entities page shared_ptr
  static inline SemaphoreHandle_t _entities_page_mutex;

  // Task handle to task that loads all rooms via MQTT
  static inline TaskHandle_t _load_all_rooms_task_handle;

  // Arguments used to initialize the local event loop for RoomManager events
  static inline esp_event_loop_args_t _local_event_loop_args;

  // Event loop used for RoomManager events
  static inline esp_event_loop_handle_t _local_event_loop = NULL;

  // Indicate wether we should load new room updates or not. This is used to disable loading of new rooms / room updates when updating firmware.
  static inline bool _load_new_rooms = true;
};