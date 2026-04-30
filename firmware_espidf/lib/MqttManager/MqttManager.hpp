#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mqtt_client.h>
#include <string>
#include <vector>

class MqttManager {
public:
  /**
   * @brief Start the MQTT client and try to connecto to @param server with given port, username and password
   * @param server: Address to MQTT server
   * @param port: port number used to connect to MQTT server
   * @param username: Login username, blank if anonymous
   * @param password: Login password, blank if anonymous
   */
  static void start(std::string *server, uint16_t *port, std::string *username, std::string *password);

  /**
   * @brief Check if currently connected to MQTT
   * @return True if connected, otherwise false
   */
  static bool connected();

  /**
   * @brief Subscribe to given MQTT topic.
   * @param mqtt_topic: The MQTT topic to subscribe to.
   * @return ESP_OK in case it was successful or ESP_ERR_NOT_FINISHED in case subscription failed.
   */
  static esp_err_t subscribe(std::string mqtt_topic);

  /**
   * @brief Unsubscribe to given MQTT topic.
   * @param mqtt_topic: The MQTT topic to unsubscribe from.
   * @return ESP_OK in case it was successful or ESP_ERR_NOT_FINISHED in case unsubscription failed.
   */
  static esp_err_t unsubscribe(std::string mqtt_topic);

  /**
   * @brief Send data to the given topic
   * @param topic: The MQTT topic to send data to
   * @param data: The data to send
   * @param data_length: The length of the data to send (number of bytes)
   * @param retian: Should MQTT retain the message
   * @return ESP_OK in case it was successful or ESP_ERR_NOT_FINISHED in case publish failed.
   */
  static esp_err_t publish(std::string topic, const char *data, size_t data_length, bool retain);

  /**
   * @brief Register event handler for MQTT events.
   * @param event_id: The MQTT event id to subscribe to.
   * @param event_handler: The event handler callback to register
   * @param event_handler_arg: Any argument to the event handler.
   */
  static esp_err_t register_handler(esp_mqtt_event_id_t event_id, esp_event_handler_t event_handler, void *event_handler_arg);

  /**
   * @brief Unregister event handler for MQTT events.
   * @param event_id: The MQTT event id to subscribe to.
   * @param event_handler: The event handler callback to register
   */
  static esp_err_t unregister_handler(esp_mqtt_event_id_t event_id, esp_event_handler_t event_handler);

private:
  /**
   * @brief Handle MQTT events.
   */
  static void _mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * Task that publishes the "online" retained status message, retrying every 5s
   * until successful. Spawned on MQTT_EVENT_CONNECTED; cancelled on
   * MQTT_EVENT_DISCONNECTED or when a new CONNECTED event supersedes it.
   */
  static void _task_send_online_update(void *param);

  /**
   * The configuration used to init and setup the MQTT client.
   */
  static inline esp_mqtt_client_config_t _mqtt_config;

  /**
   * The underlying MQTT client that is used
   */
  static inline esp_mqtt_client_handle_t _mqtt_client;

  /**
   * Are we connected to the MQTT manager?
   */
  static inline bool _connected = false;

  /**
   * Topic to send state updates (online/offline) to when connected/disconnected
   */
  static inline std::string _state_topic;

  /**
   * When disconnected from MQTT, send this message to tell all other entities on MQTT that the panel is offline.
   */
  static inline std::string _last_will_message;

  /**
   * Pre-built "online" JSON payload published by _task_send_online_update.
   * Built once in start() so the task holds no heap allocation of its own
   * and can be safely deleted at any time.
   */
  static inline std::string _online_status_message;

  /**
   * Handle of the currently live online-status publish task, or NULL.
   * Written only from the MQTT event handler (serialised) and from the
   * task itself on successful completion.
   */
  static inline TaskHandle_t _send_online_update_task_handle = NULL;
};