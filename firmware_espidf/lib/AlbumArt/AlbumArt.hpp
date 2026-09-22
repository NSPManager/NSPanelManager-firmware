#pragma once
#include <atomic>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdint.h>
#include <string>

/**
 * @brief Proof-of-concept album art renderer.
 *
 * The Nextion has no way to display an arbitrary bitmap: set_component_pic() only selects a
 * picture already baked into the TFT, and the whmi-wri path in Nextion::start_update() replaces
 * the whole TFT firmware. The only arbitrary-pixel primitive is Nextion::fill(), a solid RGB565
 * rectangle, so album art is drawn as a mosaic of filled blocks.
 *
 * Each fill is roughly 27 bytes of ASCII on a 115200 baud UART, so ~430 blocks per second. A
 * pixel-per-fill render of a 100x100 image is ~23 seconds, which is why the earlier attempt in
 * ScreensaverPage.cpp:95-135 was abandoned. Instead this renders in progressively finer passes
 * (8x8, then 16x16, then 32x32) so something appears in ~150 ms and sharpens from there.
 *
 * The manager does the scaling. NSPanelManager PR #385 serves
 *   GET /nextion-img/media_player/<id>/album_art?v=<hash>[&w=<W>&h=<H>][&format=rgb565|png]
 * returning raw little-endian RGB565, row major from the top left, center cropped to fill W x H.
 * So each pass just asks for the size it wants -- no full size buffer and no averaging on the panel.
 *
 * The art is drawn straight over whatever page is displayed. fill() uses absolute screen
 * coordinates and does not touch component definitions, so touch keeps working and navigating
 * away clears it.
 */
class AlbumArt {
public:
  /**
   * @brief Render album art from the given URL, in progressively finer passes.
   *
   * Returns immediately; the work happens on its own low priority task. A request made while a
   * render is already running is ignored rather than queued.
   * @param url: The album_art_url from NSPanelEntityState__MediaPlayer. Size parameters are
   *             appended per pass, so this should not already carry w/h.
   */
  static void render(const std::string &url);

private:
  /**
   * @brief Task body. Takes ownership of a heap allocated std::string holding the base URL.
   */
  static void _task_render(void *param);

  /**
   * @brief Fetch expected_size bytes of RGB565 from url into buffer.
   */
  static esp_err_t _fetch(const std::string &url, uint8_t *buffer, size_t expected_size);

  /**
   * @brief Draw one square pass of grid x grid blocks from a buffer of RGB565 pixels.
   */
  static void _draw_pass(const uint8_t *buffer, uint16_t grid);

  static inline std::atomic<bool> _rendering = false;

  // Where the art is drawn. The NSPanel display is 480x320, and 160 divides evenly by every
  // grid size below (20, 10 and 5 pixel blocks) so no pass leaves gaps.
  static constexpr uint16_t draw_origin_x = 160;
  static constexpr uint16_t draw_origin_y = 80;
  static constexpr uint16_t draw_size = 160;

  // Passes, coarsest first. Each overwrites the previous one.
  static constexpr uint16_t passes[3] = {8, 16, 32};
  static constexpr uint16_t max_grid = 32;

  // Two bytes per pixel, RGB565.
  static constexpr size_t max_buffer_size = max_grid * max_grid * 2;
};
