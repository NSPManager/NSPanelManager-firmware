#pragma once
#include <esp_err.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <freertos/semphr.h>
#include <vector>

class UpdateManager {
public:
  /**
   * Initialize the UpdateManager. Attach event hooks and so on.
   */
  static void init();

  /**
   * Will start downloading the GUI file from the manager and set the Nextion display in update mode.
   * Will then transfer chunk by chunk from manager into the screen.
   */
  static void update_gui(void* param);

  /**
   * Update firmware OTA from manager. This will also call update_littlefs.
   * Will compare stored MD5 checksum of installed firmware with the checksum in the manager.
   */
  static void update_firmware(void* param);

  /**
   * Update LittleFS OTA from manager.
   * Will compare stored MD5 checksum of installed LittleFS with the checksum in the manager.
   */
  static void update_littlefs(void* param);

private:
  /**
   * @brief Handle events triggered from MQTT places.
   */
  static void _mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * Update the firmware of the ESP32 chip OTA from manager
   */
  static esp_err_t _update_firmware_ota();

  /**
   * Update the firmware of the ESP32 chip OTA from manager
   */
  static esp_err_t _update_littlefs_ota();

  /**
   * Verify that the remote firmware image is suitable for this ESP32 chip
   */
  static esp_err_t _validate_image_header(esp_app_desc_t *new_app_info);

  /**
   * Attempt to download data from a URL into a vector.
   * @param return_data: Where to save received data.
   * @param download_url: The URL to try to download data from
   * @return Will return ESP_OK in case everything was OK, otherwise ESP_ERR_NOT_FINISHED.
   */
  static inline esp_err_t _download_data(std::vector<uint8_t> *return_data, const char *download_url);

  /**
   * Handle event from ESP HTTP client
   */
  static inline esp_err_t _http_event_handler(esp_http_client_event_t *event);

  // Vars:
  // Pointer to where to save downloaded data.
  static inline std::vector<uint8_t> *_download_data_store = nullptr;

  // Mutex so that _download_data_store is only accessed from one task at the time.
  static inline SemaphoreHandle_t _download_data_store_mutex;

  // If an update task is running, this is a handle for that task.
  static inline TaskHandle_t _current_update_task = NULL;
};