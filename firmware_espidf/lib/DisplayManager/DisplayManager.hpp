#pragma once
#include <cstdint>
#include <esp_err.h>
#include <lvgl.h>

/**
 * Owns the display hardware and LVGL: display power, the UART to the panel,
 * the NSPanelDisplay driver, the LVGL display and the task running LVGL.
 *
 * Knows nothing about pages: what to show is InterfaceManager's job.
 */
class DisplayManager {
public:
  static constexpr uint16_t WIDTH = 454;
  static constexpr uint16_t HEIGHT = 320;

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

  // Partial render buffer: a tenth of the screen in RGB565.
  static constexpr size_t _DISPLAY_BUFFER_SIZE = (WIDTH * HEIGHT) / 10 * 2;
  static inline uint8_t _display_buf[_DISPLAY_BUFFER_SIZE];

  static inline lv_display_t *_display = nullptr;
};
