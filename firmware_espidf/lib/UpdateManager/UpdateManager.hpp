#pragma once
#include <esp_err.h>
#include <esp_http_client.h>
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
  static void update_gui();

  /**
   * Update firmware OTA from manager. This will also call update_littlefs.
   * Will compare stored MD5 checksum of installed firmware with the checksum in the manager.
   */
  static void update_firmware();

  /**
   * Update LittleFS OTA from manager.
   * Will compare stored MD5 checksum of installed LittleFS with the checksum in the manager.
   */
  static void update_littlefs();

private:
  /**
   * @brief Handle events triggered from MQTT places.
   */
  static void _mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * Update the firmware of the ESP32 chip OTA from manager
   */
  static void _update_firmware_ota();

  /**
   * Update the stored MD5 checksum of installed firmware
   */
  static void _update_firmware_stored_md5();

  /**
   * Update the firmware of the ESP32 chip OTA from manager
   */
  static void _update_littlefs_ota();

  /**
   * Update the stored MD5 checksum of installed littlefs
   */
  static void _update_littlefs_stored_md5();

  /**
   * Attempt to download data from a URL into a vector.
   * @param return_string: Where to save received data.
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
  static inline SemaphoreHandle_t _download_data_store_mutex = NULL;
};