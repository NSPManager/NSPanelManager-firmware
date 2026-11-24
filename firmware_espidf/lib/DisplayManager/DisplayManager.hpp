#pragma once
#include <cstdint>
#include <ConfigManager.hpp>
#include <esp_err.h>
#include <lvgl.h>
#include <protobuf_nspanel.pb-c.h>

/**
 * Owns the display hardware and LVGL: display power, the UART to the panel,
 * the NSPanelDisplay driver, the LVGL display and the task running LVGL.
 *
 * Knows nothing about pages: what to show is InterfaceManager's job.
 */
class DisplayManager {
public:
  // Size of the landscape display: EU panels, and US panels mounted horizontally. The pages are laid out for it.
  static constexpr uint16_t LANDSCAPE_WIDTH = 454;
  static constexpr uint16_t LANDSCAPE_HEIGHT = 320;
  // Size of the portrait display: US panels mounted vertically.
  static constexpr uint16_t PORTRAIT_WIDTH = 320;
  static constexpr uint16_t PORTRAIT_HEIGHT = 480;

  /**
   * @brief Size of the display, from the orientation stored in the config when init() ran.
   */
  static uint16_t width() { return _width; }
  static uint16_t height() { return _height; }
  static ConfigManager::DisplayOrientation orientation() { return _orientation; }

  /**
   * @brief Power up the display, set up its UART, LVGL and the display driver, and start the LVGL task.
   * When this returns ESP_OK the display is ready for LVGL calls (under a Lock).
   */
  static esp_err_t init();

  /**
   * @brief Has init() completed successfully?
   */
  static bool ready() { return _display != nullptr; }

  /**
   * @brief The LVGL display, or nullptr before init() has succeeded.
   */
  static lv_display_t *display() { return _display; }

  /**
   * @brief Set the display backlight brightness, 0-100. Safe from any task.
   */
  static void set_brightness(int percent);

  /**
   * @brief Store the display orientation from the manager's config. The display is set up at boot,
   * so when it differs from the orientation in use, the panel restarts to apply it.
   */
  static void apply_orientation_from_config(NSPanelConfig__NSPanelDisplayOrientation orientation);

  /**
   * Holds the LVGL lock for its lifetime. Every LVGL call made outside the LVGL
   * task must be made while holding one:
   *
   *   DisplayManager::Lock lock;
   *   lv_label_set_text(label, "...");
   */
  class Lock {
  public:
    Lock() { lv_lock(); }
    ~Lock() { lv_unlock(); }
    Lock(const Lock &) = delete;
    Lock &operator=(const Lock &) = delete;
  };

private:
  static esp_err_t _init_uart();
  static void _task_lvgl(void *param);
  static void _task_restart_for_orientation(void *param);

  // Partial render buffer: a tenth of the larger of the two screens, in RGB565.
  static constexpr size_t _DISPLAY_BUFFER_SIZE =
      (LANDSCAPE_WIDTH * LANDSCAPE_HEIGHT > PORTRAIT_WIDTH * PORTRAIT_HEIGHT ? LANDSCAPE_WIDTH * LANDSCAPE_HEIGHT : PORTRAIT_WIDTH * PORTRAIT_HEIGHT) / 10 * 2;
  // LVGL rejects (silently, at our log level) a buffer that is not aligned to LV_DRAW_BUF_ALIGN, and then
  // renders nothing at all. A plain uint8_t array has no alignment of its own, so ask for it explicitly.
  alignas(LV_DRAW_BUF_ALIGN) static inline uint8_t _display_buf[_DISPLAY_BUFFER_SIZE];

  static inline uint16_t _width = LANDSCAPE_WIDTH;
  static inline uint16_t _height = LANDSCAPE_HEIGHT;
  static inline ConfigManager::DisplayOrientation _orientation = ConfigManager::DisplayOrientation::LANDSCAPE;
  static inline bool _restart_pending = false;

  static inline lv_display_t *_display = nullptr;
};
