#pragma once
#include <lvgl.h>
#include <vector>

/**
 * The panel's background image: one image, shown behind every page that
 * attaches its screen (all pages except the loading page).
 *
 * The image is made by the manager: raw RGB565 (little endian) at the size of
 * the display, so the panel needs no image decoder. It is downloaded from
 *   GET http://<manager>:<port>/download_background/<nspanel_id>
 * see ManagerFile for how it is downloaded and stored (404: no background).
 * It is kept in PSRAM while the panel runs.
 */
class BackgroundManager {
public:
  /**
   * Load the stored image from LittleFS and register for downloads. Call once LittleFS is mounted
   * and the display is set up, before ManagerFile::request_updates().
   */
  static void init();

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
  /**
   * Download the image if it has changed, and show it. Runs on the ManagerFile task.
   */
  static void _update();

  /**
   * Show this image on every attached screen and free the previous one. Takes ownership of pixels (PSRAM). nullptr removes the background.
   */
  static void _set_image(uint8_t *pixels);

  // Guarded by the LVGL lock
  static inline lv_image_dsc_t *_image = nullptr;
  static inline std::vector<lv_obj_t *> _screens;
};
