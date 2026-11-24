#include <ConfigManager.hpp>
#include <DisplayManager.hpp>
#include <LoadingPage.hpp>
#include <StatusUpdateManager.hpp>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <nspanel_terminal.h>

#if defined(BOARD_SONOFF)
#define DISPLAY_POWER_GPIO GPIO_NUM_4
#define DISPLAY_UART_TX_GPIO GPIO_NUM_16
#define DISPLAY_UART_RX_GPIO GPIO_NUM_17
#elif defined(BOARD_CUSTOM)
#define DISPLAY_POWER_GPIO GPIO_NUM_9
#define DISPLAY_UART_TX_GPIO GPIO_NUM_3
#define DISPLAY_UART_RX_GPIO GPIO_NUM_46
#endif

#define DISPLAY_UART UART_NUM_2

esp_err_t DisplayManager::init() {
  esp_log_level_set("DisplayManager", ConfigManager::log_level);
  if (DisplayManager::ready()) {
    return ESP_OK;
  }

  // Power cycle the display. The power switch is active low.
  gpio_set_direction(DISPLAY_POWER_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_level(DISPLAY_POWER_GPIO, 1);
  vTaskDelay(pdMS_TO_TICKS(250)); // Wait for screen to turn off
  gpio_set_level(DISPLAY_POWER_GPIO, 0);

  esp_err_t err = DisplayManager::_init_uart();
  if (err != ESP_OK) {
    return err;
  }

  DisplayManager::_orientation = ConfigManager::display_orientation;
  switch (DisplayManager::_orientation) {
  case ConfigManager::DisplayOrientation::PORTRAIT:
    DisplayManager::_width = PORTRAIT_WIDTH;
    DisplayManager::_height = PORTRAIT_HEIGHT;
    ESP_LOGI("DisplayManager", "Display orientation: portrait, %ux%u.", DisplayManager::_width, DisplayManager::_height);
    ESP_LOGW("DisplayManager", "There are no portrait page layouts yet: pages are laid out for landscape.");
    break;
  case ConfigManager::DisplayOrientation::LANDSCAPE_MIRRORED:
    DisplayManager::_width = LANDSCAPE_WIDTH;
    DisplayManager::_height = LANDSCAPE_HEIGHT;
    ESP_LOGW("DisplayManager", "Display orientation: mirrored landscape, %ux%u. Mirroring is not supported yet, shown as landscape.",
             DisplayManager::_width, DisplayManager::_height);
    break;
  case ConfigManager::DisplayOrientation::LANDSCAPE:
  default:
    DisplayManager::_width = LANDSCAPE_WIDTH;
    DisplayManager::_height = LANDSCAPE_HEIGHT;
    ESP_LOGI("DisplayManager", "Display orientation: landscape, %ux%u.", DisplayManager::_width, DisplayManager::_height);
    break;
  }

  nspanel_terminal::NSPanelDisplay::Config config = {
      .uart_port = DISPLAY_UART,
      .width = DisplayManager::_width,
      .height = DisplayManager::_height,
      .initial_brightness = 100,
      .tft_url = "",
  };

  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return (uint32_t)(esp_timer_get_time() / 1000); });

  lv_display_t *display = lv_display_create(DisplayManager::_width, DisplayManager::_height);
  if (display == nullptr) {
    ESP_LOGE("DisplayManager", "Failed to create LVGL display!");
    return ESP_ERR_NO_MEM;
  }
  lv_display_set_buffers(display, DisplayManager::_display_buf, NULL, sizeof(DisplayManager::_display_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  if (lv_display_get_buf_active(display) == nullptr) {
    // lv_display_set_buffers() returns nothing: a rejected buffer only shows as a display with no buffer.
    ESP_LOGE("DisplayManager", "LVGL rejected the draw buffer at %p (alignment?). Nothing can be drawn!", (void *)DisplayManager::_display_buf);
    return ESP_FAIL;
  }
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);

  auto &nspanel_display = nspanel_terminal::NSPanelDisplay::instance();
  err = nspanel_display.begin(config);
  if (err != ESP_OK) {
    ESP_LOGE("DisplayManager", "Failed to start display driver! Error: %s", esp_err_to_name(err));
    return err;
  }
  if (!nspanel_display.attach_lvgl(display)) {
    ESP_LOGE("DisplayManager", "Failed to attach LVGL to the display driver!");
    return ESP_FAIL;
  }

  // Start the LVGL task only once the flush callback is installed: a refresh
  // without a flush_cb leaves disp->flushing set forever and the next refresh
  // spins in wait_for_flushing().
  if (xTaskCreatePinnedToCore(DisplayManager::_task_lvgl, "lvgl", 8192, nullptr, 2, nullptr, 1) != pdPASS) {
    ESP_LOGE("DisplayManager", "Failed to start LVGL task!");
    return ESP_ERR_NO_MEM;
  }

  DisplayManager::_display = display;
  ESP_LOGI("DisplayManager", "Display initialized.");
  return ESP_OK;
}

esp_err_t DisplayManager::_init_uart() {
  uart_config_t uart_config = {};
  uart_config.baud_rate = (int)ConfigManager::communication_baud_rate;
  uart_config.data_bits = UART_DATA_8_BITS;
  uart_config.parity = UART_PARITY_DISABLE;
  uart_config.stop_bits = UART_STOP_BITS_1;
  uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uart_config.source_clk = UART_SCLK_DEFAULT;

  esp_err_t err = uart_param_config(DISPLAY_UART, &uart_config);
  if (err != ESP_OK) {
    ESP_LOGE("DisplayManager", "Failed to set UART parameters! Error: %s", esp_err_to_name(err));
    return err;
  }

  QueueHandle_t uart_event_queue;
  err = uart_driver_install(DISPLAY_UART, 4096 * 2, 6000, 16, &uart_event_queue, 0);
  if (err != ESP_OK) {
    ESP_LOGE("DisplayManager", "Failed to install UART driver! Error: %s", esp_err_to_name(err));
    return err;
  }

  // Set RX pin to input mode with pull-up
  gpio_config_t rx_io_conf = {};
  rx_io_conf.mode = GPIO_MODE_INPUT;
  rx_io_conf.pin_bit_mask = 1ULL << DISPLAY_UART_RX_GPIO;
  rx_io_conf.intr_type = GPIO_INTR_DISABLE;
  rx_io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  rx_io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  err = gpio_config(&rx_io_conf);
  if (err != ESP_OK) {
    ESP_LOGE("DisplayManager", "Failed to configure UART RX pin! Error: %s", esp_err_to_name(err));
    return err;
  }

  err = uart_set_pin(DISPLAY_UART, DISPLAY_UART_TX_GPIO, DISPLAY_UART_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (err != ESP_OK) {
    ESP_LOGE("DisplayManager", "Failed to set UART pins! Error: %s", esp_err_to_name(err));
    return err;
  }
  return ESP_OK;
}

void DisplayManager::apply_orientation_from_config(NSPanelConfig__NSPanelDisplayOrientation orientation) {
  ConfigManager::DisplayOrientation new_orientation;
  switch (orientation) {
  case NSPANEL_CONFIG__NSPANEL_DISPLAY_ORIENTATION__LANDSCAPE_MIRRORED:
    new_orientation = ConfigManager::DisplayOrientation::LANDSCAPE_MIRRORED;
    break;
  case NSPANEL_CONFIG__NSPANEL_DISPLAY_ORIENTATION__PORTRAIT:
    new_orientation = ConfigManager::DisplayOrientation::PORTRAIT;
    break;
  default: // Including managers too old to send it: they only know landscape interfaces for this firmware.
    new_orientation = ConfigManager::DisplayOrientation::LANDSCAPE;
    break;
  }

  if (new_orientation == DisplayManager::_orientation || DisplayManager::_restart_pending) {
    return;
  }

  ESP_LOGI("DisplayManager", "Display orientation changed from %d to %d. Will restart to apply it.", static_cast<int>(DisplayManager::_orientation),
           static_cast<int>(new_orientation));
  ConfigManager::display_orientation = new_orientation;
  if (ConfigManager::save_config() != ESP_OK) {
    ESP_LOGE("DisplayManager", "Failed to save the new display orientation. Will not restart.");
    return;
  }
  // Restart from a task of its own: this is called from an event handler, and the restart waits for
  // the status report to be sent.
  DisplayManager::_restart_pending = true;
  if (xTaskCreatePinnedToCore(DisplayManager::_task_restart_for_orientation, "orientation_rst", 4096, NULL, 2, NULL, 1) != pdPASS) {
    ESP_LOGE("DisplayManager", "Failed to start restart task. The new orientation is applied on the next restart.");
  }
}

void DisplayManager::_task_restart_for_orientation(void *param) {
  LoadingPage::show();
  LoadingPage::set_loading_text("Changing display orientation");
  LoadingPage::set_secondary_text("");
  vTaskDelay(pdMS_TO_TICKS(500)); // Let the message reach the display
  StatusUpdateManager::reboot(); // Does not return.
}

void DisplayManager::set_brightness(int percent) {
  if (!DisplayManager::ready()) {
    return;
  }
  nspanel_terminal::NSPanelDisplay::instance().request_brightness(percent);
}

void DisplayManager::_task_lvgl(void *param) {
  for (;;) {
    uint32_t wait_ms = lv_timer_handler(); // locks internally with LV_USE_OS
    if (wait_ms == LV_NO_TIMER_READY || wait_ms > 20)
      wait_ms = 20;
    if (wait_ms < 1)
      wait_ms = 1;
    vTaskDelay(pdMS_TO_TICKS(wait_ms));
  }
}
