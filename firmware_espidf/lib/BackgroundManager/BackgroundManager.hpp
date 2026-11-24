#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <string>
#include <vector>

/**
 * The panel's background image: one image, shown behind every page that
 * attaches its screen (all pages except the loading page).
 *
 * The image is made by the manager: raw RGB565 (little endian) at the size of
 * the display, so the panel needs no image decoder. It is downloaded from
 *   GET http://<manager>:<port>/download_background/<nspanel_id>
 * sending the ETag of the stored image in If-None-Match. The manager answers
 *   200 with the image and its ETag, 304 when the stored image is current,
 *   404 when no background is configured for this panel.
 * The image is stored in LittleFS, so it is only downloaded when it changes,
 * and kept in PSRAM while the panel runs.
 */
class BackgroundManager {
public:
  /**
   * Load the stored image from LittleFS and start the download task. Call once LittleFS is mounted.
   */
  static void init();

  /**
   * Check the manager for a new background image, in the background. Safe to call from any task.
   */
  static void request_update();

  /**
   * Show the background image behind this screen, now and whenever the image changes.
   * Call with the LVGL lock held, once per screen.
   */
  static void attach(lv_obj_t *screen);

  /**
   * Is there a background image to show? Call with the LVGL lock held.
   */
  static bool has_image();

private:
  static void _task_update(void *param);

  /**
   * Download the image if it has changed. Returns true when the shown image changed.
   */
  static bool _download();

  /**
   * Show this image on every attached screen and free the previous one. Takes ownership of pixels (PSRAM). nullptr removes the background.
   */
  static void _set_image(uint8_t *pixels);

  static bool _read_stored_etag(std::string *etag);
  static uint8_t *_load_stored_image();
  static void _store_image(const uint8_t *pixels, const std::string &etag);
  static void _remove_stored_image();
  static void _log_store_failure(const char *step);

  static inline TaskHandle_t _task = NULL;

  // Guarded by the LVGL lock
  static inline lv_image_dsc_t *_image = nullptr;
  static inline std::vector<lv_obj_t *> _screens;
};
