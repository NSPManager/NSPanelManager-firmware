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
  static void update_gui(void *param);

  /**
   * Update firmware OTA from manager. This will also call update_littlefs.
   * Will compare stored MD5 checksum of installed firmware with the checksum in the manager.
   */
  static void update_firmware(void *param);

  /**
   * Update LittleFS OTA from manager.
   * Will compare stored MD5 checksum of installed LittleFS with the checksum in the manager.
   */
  static void update_littlefs(void *param);

private:
  /**
   * @brief Handle events triggered from MQTT places.
   */
  static void _mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * @brief Handle events triggered from Nextion display.
   */
  static void _nextion_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * Update the firmware of the ESP32 chip OTA from manager
   */
  static esp_err_t _update_firmware_ota();

  /**
   * Update the firmware of the ESP32 chip OTA from manager
   */
  static esp_err_t _update_littlefs_ota();

  /**
   * Attempt to download data from a URL into a vector.
   * @param return_data: Where to save received data.
   * @param download_url: The URL to try to download data from
   * @param offset: Start downloading data from an offset from the remote file. -1 will not set header.
   * @param length: Number of bytes to download from the remote file. -1 will not set header.
   * @return Will return ESP_OK in case everything was OK, otherwise ESP_ERR_NOT_FINISHED.
   */
  static esp_err_t _download_data(std::vector<uint8_t> *return_data, const char *download_url, int64_t offset, int64_t length);

  /**
   * Get remote file size
   * @param remote_file_url: URL to remote file to check size of.
   * @param file_size: Pointer to where to store file size
   * @return Will return ESP_OK in case everything was OK, otherwise ESP_ERR_NOT_FINISHED.
   */
  static esp_err_t _get_remote_file_size(std::string remote_file_url, size_t *file_size);

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

  // Next Nextion update TFT update offset.
  static inline uint64_t _nextion_update_current_offset = 0;

  // How many bytes were last written to the Nextion display
  static inline uint64_t _nextion_update_last_written_bytes = 0;
};