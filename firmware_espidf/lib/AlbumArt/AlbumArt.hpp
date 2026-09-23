#pragma once
#include <atomic>
#include <esp_err.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdint.h>
#include <string>

/**
 * @brief Progressive album art renderer.
 *
 * The Nextion has no way to display an arbitrary bitmap: set_component_pic() only selects a
 * picture already baked into the TFT, and the whmi-wri path in Nextion::start_update() replaces
 * the whole TFT firmware. The only arbitrary-pixel primitive is Nextion::fill(), a solid RGB565
 * rectangle, so album art is drawn as a mosaic of filled rectangles.
 *
 * Each fill is roughly 24 bytes of ASCII on a 115200 baud UART, so ~480 per second. The art is
 * drawn in progressively finer passes, ending at draw_size x draw_size where one block is one
 * pixel and the image is exactly the source. Something appears in ~150 ms and sharpens from there.
 *
 * Drawn naively that last pass is 25600 fills, ~614 KB, ~53 seconds -- which is why the earlier
 * attempt in ScreensaverPage.cpp:95-135 was abandoned. Two things make it affordable, both
 * exploiting that album art is mostly smooth:
 *
 *  - Run merging. One fill can span any width, so a row of consecutive blocks sharing a colour
 *    is emitted as a single fill. The cost of a run is the cost of one block.
 *  - Pass differencing. Skipping a fill leaves the previous pass's colour on screen, so the
 *    screen always holds exactly the previous pass's image. Keeping that image lets each pass
 *    emit only the runs whose colour actually changed.
 *
 * Both are data dependent, so every pass logs the fills it emitted against the blocks it covered.
 * That ratio is the number to watch when deciding whether this needs a faster UART.
 *
 * Measured on real artwork: 21158 fills, 45 s for the full ladder, at a flat ~470 fills/s across
 * every pass -- which is exactly the 115200 baud line rate, so this is wire bound with no
 * headroom. Three things would speed it up, none of them applied here: merging runs vertically
 * as well as horizontally, quantizing colour to lengthen runs, and raising the UART baud rate.
 * TFT_SPEEDUP.md has the measurements and the tradeoffs.
 *
 * The manager does the scaling. NSPanelManager PR #385 serves
 *   GET /nextion-img/media_player/<id>/album_art?v=<hash>[&w=<W>&h=<H>][&format=rgb565|png]
 * returning raw little-endian RGB565, row major from the top left, center cropped to fill W x H.
 * So each pass asks for the size it wants and is consumed a row at a time -- the full image is
 * never buffered, and nothing is scaled or averaged on the panel.
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

  /**
   * @brief Ask an in-progress render to stop.
   *
   * The full ladder takes seconds, so a render outlives the page that started it. Without this
   * it would keep painting over whatever the user navigated to. Returns immediately; the task
   * stops at its next row boundary.
   */
  static void cancel();

private:
  /**
   * @brief Task body. Takes ownership of a heap allocated std::string holding the base URL.
   */
  static void _task_render(void *param);

  /**
   * @brief Run one pass: fetch grid x grid pixels and draw the runs that changed since previous.
   *
   * Consumes the response a row at a time. If keep is non-NULL the pass is also written there,
   * to become the next pass's previous image.
   * @param previous: The image currently on screen, previous_grid x previous_grid, or NULL for
   *                  the first pass, in which case every block is drawn.
   * @return ESP_OK if the whole pass was drawn, an error if it was cut short.
   */
  static esp_err_t _run_pass(const std::string &base_url, uint16_t grid, const uint8_t *previous, uint16_t previous_grid, uint8_t *keep);

  /**
   * @brief Draw one row of a pass, merging runs and skipping what has not changed.
   * @return The number of fills emitted.
   */
  static uint32_t _draw_row(const uint8_t *row, uint16_t grid, uint16_t row_index, const uint8_t *previous, uint16_t previous_grid);

  /**
   * @brief Read exactly size bytes, looping over partial reads.
   */
  static esp_err_t _read_exact(esp_http_client_handle_t client, uint8_t *buffer, size_t size);

  /**
   * @brief RGB565 little endian, as documented on the manager's endpoint.
   */
  static inline uint16_t _pixel_at(const uint8_t *image, uint16_t grid, uint16_t row, uint16_t col) {
    size_t offset = ((size_t)row * grid + col) * 2;
    return (uint16_t)image[offset] | ((uint16_t)image[offset + 1] << 8);
  }

  static inline std::atomic<bool> _rendering = false;
  static inline std::atomic<bool> _cancelled = false;

  // Where the art is drawn. The NSPanel display is 480x320. Every grid below divides draw_size
  // exactly, so no pass leaves gaps.
  static constexpr uint16_t draw_origin_x = 160;
  static constexpr uint16_t draw_origin_y = 80;
  static constexpr uint16_t draw_size = 160;

  // Passes, coarsest first, ending at one block per pixel. Block sizes: 20, 10, 5, 2, 1.
  static constexpr uint16_t passes[5] = {8, 16, 32, 80, 160};

  // Two bytes per pixel, RGB565. Only one row of the finest pass is ever held.
  static constexpr size_t max_row_bytes = draw_size * 2;
};
