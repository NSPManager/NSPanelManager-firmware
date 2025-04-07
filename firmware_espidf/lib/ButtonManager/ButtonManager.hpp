#ifndef BUTTON_MANAGER_HPP
#define BUTTON_MANAGER_HPP

#include <atomic>
#include <driver/gpio.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <memory>
#include <protobuf_nspanel.pb-c.h>

class ButtonManager {
public:
  static void init();

  static void init_mqtt();

private:
  static void IRAM_ATTR _interrupt_triggered(void *param);

  /**
   * Task that will be interrupted to actually handle the event.
   */
  static void _interrupt_handle_task(void *param);

  /**
   * Set the state of a relay and send state update to MQTT
   */
  static void _set_relay_state(uint8_t relay, bool state, bool send_mqtt_update);

  /**
   * Get the current output state of a relay
   */
  static bool _get_relay_state(uint8_t relay);

  /**
   * @brief Handle any event trigger from the NSPM_ConfigManager
   */
  static void _nspm_configmanager_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * @brief Handle data received over MQTT
   */
  static void _mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  // Settings
  static inline std::atomic<bool> _reverse_relays = false;      // Should relays be reversed/flipped?
  static inline std::atomic<bool> _relay1_default_mode = false; // Relay 1 on by default?
  static inline std::atomic<bool> _relay2_default_mode = false; // Relay 2 on by default?
  static inline std::atomic<bool> _relay1_current_state = false;
  static inline std::atomic<bool> _relay2_current_state = false;
  static inline NSPanelConfig__NSPanelButtonMode _button1_mode = NSPanelConfig__NSPanelButtonMode::NSPANEL_CONFIG__NSPANEL_BUTTON_MODE__DIRECT; // Start by using default direct mode
  static inline NSPanelConfig__NSPanelButtonMode _button2_mode = NSPanelConfig__NSPanelButtonMode::NSPANEL_CONFIG__NSPANEL_BUTTON_MODE__DIRECT; // Start by using default direct mode;

  // Queue of interrupt events to handle
  static inline QueueHandle_t _interrupt_queue;

  // Button data
  static inline gpio_config_t _button_io_config;
  static constexpr const gpio_num_t _button1_pin = gpio_num_t::GPIO_NUM_14;
  static constexpr const gpio_num_t _button2_pin = gpio_num_t::GPIO_NUM_27;
  static constexpr const uint32_t _interrupt_pin_mask = ((1ULL << _button1_pin) | (1ULL << _button2_pin));

  // Relay data
  static inline gpio_config_t _relay_io_config;
  static constexpr const gpio_num_t _relay1_pin = gpio_num_t::GPIO_NUM_22;
  static constexpr const gpio_num_t _relay2_pin = gpio_num_t::GPIO_NUM_19;
  static constexpr const uint32_t _relay_pin_mask = ((1ULL << _relay1_pin) | (1ULL << _relay2_pin));
};

#endif