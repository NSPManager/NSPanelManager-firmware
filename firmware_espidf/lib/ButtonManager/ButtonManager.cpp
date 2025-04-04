#include <ButtonManager.hpp>
#include <MqttManager.hpp>
#include <NSPM_ConfigManager.hpp> // Forward declared to allow compilation to succeed.
#include <NSPM_ConfigManager_event.hpp>
#include <driver/gpio.h>
#include <esp_log.h>
#include <vector>

void ButtonManager::init() {
  esp_log_level_set("ButtonManager", esp_log_level_t::ESP_LOG_DEBUG); // TODO: Load from config
  ButtonManager::_interrupt_queue = xQueueCreate(4, sizeof(uint32_t));

  esp_event_handler_register(NSPM_CONFIGMANAGER_EVENT, nspm_configmanager_event::CONFIG_LOADED, ButtonManager::_nspm_configmanager_event_handler, NULL);

  ButtonManager::_button_io_config = {};
  ButtonManager::_button_io_config.intr_type = GPIO_INTR_ANYEDGE;
  ButtonManager::_button_io_config.pin_bit_mask = ButtonManager::_interrupt_pin_mask;
  ButtonManager::_button_io_config.mode = GPIO_MODE_INPUT;
  ButtonManager::_button_io_config.pull_up_en = gpio_pullup_t::GPIO_PULLUP_DISABLE;
  if (gpio_config(&ButtonManager::_button_io_config) != ESP_OK) [[unlikely]] {
    ESP_LOGE("ButtonManager", "Failed to configure IO for buttons.");
  }

  ButtonManager::_relay_io_config = {};
  ButtonManager::_relay_io_config.intr_type = GPIO_INTR_DISABLE;
  ButtonManager::_relay_io_config.pin_bit_mask = ButtonManager::_relay_pin_mask;
  ButtonManager::_relay_io_config.mode = GPIO_MODE_OUTPUT;
  ButtonManager::_relay_io_config.pull_up_en = gpio_pullup_t::GPIO_PULLUP_DISABLE;
  if (gpio_config(&ButtonManager::_relay_io_config) != ESP_OK) [[unlikely]] {
    ESP_LOGE("ButtonManager", "Failed to configure IO for relays.");
  }

  xTaskCreatePinnedToCore(&ButtonManager::_interrupt_handle_task, "inter_handle_task", 4096, NULL, 1, NULL, 1); // Start task to handle interrupt events once they happen

  gpio_install_isr_service(0);
  gpio_isr_handler_add(ButtonManager::_button1_pin, ButtonManager::_interrupt_triggered, (void *)ButtonManager::_button1_pin);
  gpio_isr_handler_add(ButtonManager::_button2_pin, ButtonManager::_interrupt_triggered, (void *)ButtonManager::_button2_pin);
}

void ButtonManager::_interrupt_triggered(void *param) {
  uint32_t gpio_num = (uint32_t)param;
  xQueueSendFromISR(ButtonManager::_interrupt_queue, &gpio_num, NULL);
}

void ButtonManager::_interrupt_handle_task(void *param) {
  uint32_t io_num;
  for (;;) {
    if (xQueueReceive(ButtonManager::_interrupt_queue, &io_num, portMAX_DELAY) == pdPASS) {
      bool current_state = gpio_get_level(static_cast<gpio_num_t>(io_num));

      // Received a new interrupt, check level of button GPIO
      ESP_LOGD("ButtonManager", "Got button %ld event, new state: %s.", io_num, !current_state ? "ON" : " OFF");

      if (io_num == ButtonManager::_button1_pin) {
        switch (ButtonManager::_button1_mode) {
        case NSPanelConfig__NSPanelButtonMode::NSPANEL_CONFIG__NSPANEL_BUTTON_MODE__DIRECT: {
          if (!current_state) {                                                      // Only toggle on button press and not release
            ButtonManager::_set_relay_state(1, !ButtonManager::_get_relay_state(1)); // Toggle output
          }
          break;
        }

        case NSPanelConfig__NSPanelButtonMode::NSPANEL_CONFIG__NSPANEL_BUTTON_MODE__FOLLOW: {
          ButtonManager::_set_relay_state(1, !current_state); // When button is pressed, activate relay
          break;
        }

        case NSPanelConfig__NSPanelButtonMode::NSPANEL_CONFIG__NSPANEL_BUTTON_MODE__NOTIFY_MANAGER: {
          std::shared_ptr<NSPanelConfig> config;
          if (NSPM_ConfigManager::get_config(&config) == ESP_OK) [[likely]] {
            NSPanelMQTTManagerCommand command = NSPANEL_MQTTMANAGER_COMMAND__INIT;
            NSPanelMQTTManagerCommand__ButtonPressed pressed_command = NSPANEL_MQTTMANAGER_COMMAND__BUTTON_PRESSED__INIT;
            pressed_command.button_id = 1;
            pressed_command.nspanel_id = config->nspanel_id;
            command.button_pressed = &pressed_command;
            command.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_BUTTON_PRESSED;

            uint32_t packed_length = nspanel_mqttmanager_command__get_packed_size(&command);
            std::vector<uint8_t> buffer(packed_length); // Use vector for automatic cleanup of data when going out of scope
            size_t packed_data_size = nspanel_mqttmanager_command__pack(&command, buffer.data());
            if (packed_data_size == packed_length) [[likely]] {
              if (MqttManager::publish(NSPM_ConfigManager::get_manager_command_topic(), (const char *)buffer.data(), packed_length, false) != ESP_OK) [[unlikely]] {
                ESP_LOGE("ButtonManager", "Failed to publish command that button was pressed.");
              }
            }
          } else {
            ESP_LOGE("ButtonManager", "Failed to get config while trying to process button press event and as such could not sent event to manager for further handling.");
          }
          break;
        }

        default:
          ESP_LOGE("ButtonManager", "Unknown button action. Will not perform any action.");
          break;
        }
      } else if (io_num == ButtonManager::_button2_pin) {
        switch (ButtonManager::_button2_mode) {
        case NSPanelConfig__NSPanelButtonMode::NSPANEL_CONFIG__NSPANEL_BUTTON_MODE__DIRECT: {
          if (!current_state) {                                                      // Only toggle on button press and not release
            ButtonManager::_set_relay_state(2, !ButtonManager::_get_relay_state(2)); // Toggle output
          }
          break;
        }

        case NSPanelConfig__NSPanelButtonMode::NSPANEL_CONFIG__NSPANEL_BUTTON_MODE__FOLLOW: {
          ButtonManager::_set_relay_state(2, !current_state); // When button is pressed, activate relay
          break;
        }

        case NSPanelConfig__NSPanelButtonMode::NSPANEL_CONFIG__NSPANEL_BUTTON_MODE__NOTIFY_MANAGER: {
          std::shared_ptr<NSPanelConfig> config;
          if (NSPM_ConfigManager::get_config(&config) == ESP_OK) [[likely]] {
            NSPanelMQTTManagerCommand command = NSPANEL_MQTTMANAGER_COMMAND__INIT;
            NSPanelMQTTManagerCommand__ButtonPressed pressed_command = NSPANEL_MQTTMANAGER_COMMAND__BUTTON_PRESSED__INIT;
            pressed_command.button_id = 2;
            pressed_command.nspanel_id = config->nspanel_id;
            command.button_pressed = &pressed_command;
            command.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_BUTTON_PRESSED;

            uint32_t packed_length = nspanel_mqttmanager_command__get_packed_size(&command);
            std::vector<uint8_t> buffer(packed_length); // Use vector for automatic cleanup of data when going out of scope
            size_t packed_data_size = nspanel_mqttmanager_command__pack(&command, buffer.data());
            if (packed_data_size == packed_length) [[likely]] {
              if (MqttManager::publish(NSPM_ConfigManager::get_manager_command_topic(), (const char *)buffer.data(), packed_length, false) != ESP_OK) [[unlikely]] {
                ESP_LOGE("ButtonManager", "Failed to publish command that button was pressed.");
              }
            }
          } else {
            ESP_LOGE("ButtonManager", "Failed to get config while trying to process button press event and as such could not sent event to manager for further handling.");
          }
          break;
        }

        default:
          ESP_LOGE("ButtonManager", "Unknown button action. Will not perform any action.");
          break;
        }
      }
    }
  }
}

void ButtonManager::_set_relay_state(uint8_t relay, bool state) {
  if (relay == 1) {
    if (!ButtonManager::_reverse_relays) {
      ESP_LOGD("ButtonManager", "Setting output of relay 1 (left) relay to %s", state ? "ON" : " OFF");
      ButtonManager::_relay1_current_state = state;
      gpio_set_level(ButtonManager::_relay1_pin, state ? 1 : 0);
    } else {
      ESP_LOGD("ButtonManager", "Setting output of relay 2 (right|reversed) relay to %s", state ? "ON" : " OFF");
      ButtonManager::_relay2_current_state = state;
      gpio_set_level(ButtonManager::_relay2_pin, state ? 1 : 0);
    }
  } else if (relay == 2) {
    if (!ButtonManager::_reverse_relays) {
      ESP_LOGD("ButtonManager", "Setting output of relay 2 (right) relay to %s", state ? "ON" : " OFF");
      ButtonManager::_relay2_current_state = state;
      gpio_set_level(ButtonManager::_relay2_pin, state ? 1 : 0);
    } else {
      ESP_LOGD("ButtonManager", "Setting output of relay 1 (left|reversed) relay to %s", state ? "ON" : " OFF");
      ButtonManager::_relay1_current_state = state;
      gpio_set_level(ButtonManager::_relay1_pin, state ? 1 : 0);
    }
  }
}

bool ButtonManager::_get_relay_state(uint8_t relay) {
  if (relay == 1) {
    if (!ButtonManager::_reverse_relays) {
      return ButtonManager::_relay1_current_state;
    } else {
      return ButtonManager::_relay2_current_state;
    }
  } else if (relay == 2) {
    if (!ButtonManager::_reverse_relays) {
      return ButtonManager::_relay2_current_state;
    } else {
      return ButtonManager::_relay1_current_state;
    }
  }
  return false; // Should never be reached
}

void ButtonManager::_nspm_configmanager_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  std::shared_ptr<NSPanelConfig> config;
  if (NSPM_ConfigManager::get_config(&config) == ESP_OK) [[likely]] {
    ButtonManager::_reverse_relays = config->reverse_relays;
    ButtonManager::_button1_mode = config->button1_mode;
    ButtonManager::_button2_mode = config->button2_mode;
  }
}