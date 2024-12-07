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
   * @brief Get a room status given the ID
   * @param status: Reference to where to save NSPanelRoomStatus to
   * @param room_id: What room to get the status for
   * @return ESP_OK if operation was successful, otherwise ESP_ERR_NOT_FINISHED
   */
  static esp_err_t get_room_status(std::shared_ptr<NSPanelRoomStatus> *status, uint32_t room_id);

  /**
   * @brief Get a room status pointer to a __unpacked protobuf given the ID
   * @param status: Reference to where to save NSPanelRoomStatus to
   * @param room_id: What room to get the status for
   * @return ESP_OK if operation was successful, otherwise ESP_ERR_NOT_FINISHED
   */
  static esp_err_t get_room_status_mutable(NSPanelRoomStatus **status, uint32_t room_id);

  /**
   * @brief Get a room status object for the currently selected room
   * @param status: Reference to where to save NSPanelRoomStatus to
   * @return ESP_OK if operation was successful, otherwise ESP_ERR_NOT_FINISHED
   */
  static esp_err_t get_current_room_status(std::shared_ptr<NSPanelRoomStatus> *status);

  /**
   * @brief Get a room status pointer to a __unpacked protobuf to currently selected room
   * @param status: Reference to where to save NSPanelRoomStatus to
   * @return ESP_OK if operation was successful, otherwise ESP_ERR_NOT_FINISHED
   */
  static esp_err_t get_current_room_status_mutable(NSPanelRoomStatus **status);

  /**
   * @brief Get the ID of the currently selected room
   * @param room_id: Reference to where to save the currently selected room ID
   * @return ESP_OK if operation was successful, otherwise ESP_ERR_NOT_FINISHED
   */
  static esp_err_t get_current_room_id(int32_t *room_id);

  /**
   * @brief Get the ID of the currently selected room
   * @return ESP_OK if operation was successful, otherwise ESP_ERR_NOT_FINISHED or ESP_ERR_NOT_FOUND
   */
  static esp_err_t go_to_next_room();

  /**
   * @brief Will search for a matching room (match by room ID) and replace the one in the list of rooms
   * @param status: The new room status to be replaced into the list of rooms.
   * @return ESP_OK if operation was successful, otherwise ESP_ERR_NOT_FINISHED
   */
  static esp_err_t replace_room_status(NSPanelRoomStatus *status);

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
   * @brief Handle events triggered from other places.
   */
  static void _event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * The task that will load all rooms into memory.
   */
  static void _task_load_all_room_configs(void *param);

  /**
   * Once the NSPM config has been loaded from the MQTT Manager container, load all rooms
   */
  static void _load_all_rooms(void *arg);

  /**
   * Function used when shared_ptr goes out of scope. Used to delete the underlying object
   * using the nspanel_room_status__free_unpacked function
   */
  static void _nspanel_room_status_shared_ptr_deleter(NSPanelRoomStatus *status);

  // Vars:
  // List of all loaded rooms and their statuses
  static inline std::list<std::shared_ptr<NSPanelRoomStatus>> _room_statuses;

  // Mutex to only allow one task at the time to access the list of room statuses
  static inline SemaphoreHandle_t _room_statuses_mutex;

  // Task handle to task that loads all rooms via MQTT
  static inline TaskHandle_t _load_all_rooms_task_handle;

  // The currently selected room
  static inline uint32_t _current_room_id;

  // Arguments used to initialize the local event loop for RoomManager events
  static inline esp_event_loop_args_t _local_event_loop_args;

  // Event loop used for RoomManager events
  static inline esp_event_loop_handle_t _local_event_loop;
};