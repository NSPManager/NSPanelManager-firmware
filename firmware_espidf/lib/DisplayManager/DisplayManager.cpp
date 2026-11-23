#include <ConfigManager.hpp>
#include <DisplayManager.hpp>
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

  nspanel_terminal::NSPanelDisplay::Config config = {
      .uart_port = DISPLAY_UART,
      .width = WIDTH,
      .height = HEIGHT,
      .initial_brightness = 100,
      .tft_url = "",
  };

  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return (uint32_t)(esp_timer_get_time() / 1000); });

  lv_display_t *display = lv_display_create(WIDTH, HEIGHT);
  if (display == nullptr) {
    ESP_LOGE("DisplayManager", "Failed to create LVGL display!");
    return ESP_ERR_NO_MEM;
  }
  lv_display_set_buffers(display, DisplayManager::_display_buf, NULL, sizeof(DisplayManager::_display_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
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
