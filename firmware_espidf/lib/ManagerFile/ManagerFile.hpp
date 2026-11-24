#pragma once
#include <cstddef>
#include <cstdint>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string>
#include <vector>

/**
 * A file the panel downloads from the manager and keeps in LittleFS, such as the background image
 * or the icon pack.
 *
 * It is downloaded from
 *   GET http://<manager>:<port><url_path><nspanel_id><url_query>
 * sending the ETag of the stored copy in If-None-Match. The manager answers 200 with the file and
 * its ETag, 304 when the stored copy is current, or 404 when there is no such file for this panel.
 *
 * The file is stored as /littlefs/<name>.bin, and its ETag as /littlefs/<name>.etag after it: the
 * ETag file marks the stored file complete. LittleFS only commits a file when it is closed, so a
 * power loss leaves no file or a complete one. (rename() is not available: CONFIG_VFS_SUPPORT_DIR
 * is off.)
 *
 * Files are downloaded on one shared task, see register_updater().
 */
class ManagerFile {
public:
  enum class Result {
    UNCHANGED,  // The stored copy is current, or there is no manager or config yet
    DOWNLOADED, // A new file was downloaded
    REMOVED,    // The manager has no such file (any more); the stored copy was removed
    FAILED,
  };

  /**
   * @param name: Base name of the stored files, e.g. "background" for /littlefs/background.bin.
   * @param url_path: Path to download from, followed by the panel ID, e.g. "/download_background/".
   */
  ManagerFile(const char *name, const char *url_path);

  /**
   * Query string appended to the URL, e.g. "?sizes=48,28". Empty by default.
   */
  void set_url_query(const std::string &query) { this->_url_query = query; }

  /**
   * Download the file if it has changed. On DOWNLOADED, *data is the file in PSRAM (free it with
   * heap_caps_free()) and *size its size. A file that is not expected_size bytes (when non-zero), or
   * larger than max_size, is rejected.
   */
  Result download(uint8_t **data, size_t *size, size_t expected_size, size_t max_size);

  /**
   * Store the file just downloaded in LittleFS, with the ETag it came with.
   */
  void store(const uint8_t *data, size_t size);

  /**
   * The stored file in PSRAM (free it with heap_caps_free()), or nullptr when none is stored or it is
   * not expected_size bytes (when non-zero). *size is set to its size.
   */
  uint8_t *load(size_t *size, size_t expected_size);

  /**
   * Remove the stored file.
   */
  void remove();

  /**
   * Run update() on the shared download task whenever request_updates() is called. Call before the
   * first request_updates(). The task is started on the first registration.
   */
  static void register_updater(void (*update)());

  /**
   * Check the manager for new files, on the shared download task. Safe to call from any task.
   */
  static void request_updates();

private:
  bool _stored_file_exists(size_t expected_size);
  void _log_store_failure(const char *step, size_t size);
  static void _task(void *param);

  std::string _path;
  std::string _etag_path;
  std::string _url_path;
  std::string _url_query;
  std::string _downloaded_etag; // ETag of the file returned by the last download()

  static inline TaskHandle_t _task_handle = NULL;
  static inline std::vector<void (*)()> _updaters;
};
