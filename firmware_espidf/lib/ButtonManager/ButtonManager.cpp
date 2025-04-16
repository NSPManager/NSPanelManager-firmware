#include <ButtonManager.hpp>
#include <ConfigManager.hpp>
#include <MqttManager.hpp>
#include <NSPM_ConfigManager.hpp> // Forward declared to allow compilation to succeed.
#include <NSPM_ConfigManager_event.hpp>
#include <WiFiManager.hpp>
#include <driver/gpio.h>
#include <esp_log.h>
#include <format>
#include <vector>

void ButtonManager::init() {
  esp_log_level_set("ButtonManager", ConfigManager::log_level);
  ButtonManager::_interrupt_queue = xQueueCreate(4, sizeof(uint32_t));

  ButtonManager::_relay1_default_mode = ConfigManager::relay1_default_mode;
  ButtonManager::_relay2_default_mode = ConfigManager::relay2_default_mode;

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

void ButtonManager::init_mqtt() {
  // Setup and subscribe to MQTT
  MqttManager::register_handler(MQTT_EVENT_ANY, &ButtonManager::_mqtt_event_handler, NULL);

  std::string relay1_topic = std::format("nspanel/{}/relay1_cmd", WiFiManager::mac_string());
  std::string relay2_topic = std::format("nspanel/{}/relay2_cmd", WiFiManager::mac_string());
  MqttManager::subscribe(relay1_topic);
  MqttManager::subscribe(relay2_topic);
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
      // ESP_LOGD("ButtonManager", "Got button %ld event, new state: %s.", io_num, !current_state ? "ON" : " OFF");

      if (io_num == ButtonManager::_button1_pin) {
        switch (ButtonManager::_button1_mode) {
        case NSPanelConfig__NSPanelButtonMode::NSPANEL_CONFIG__NSPANEL_BUTTON_MODE__DIRECT: {
          if (!current_state) {                                                            // Only toggle on button press and not release
            ButtonManager::_set_relay_state(1, !ButtonManager::_get_relay_state(1), true); // Toggle output
          }
          break;
        }

        case NSPanelConfig__NSPanelButtonMode::NSPANEL_CONFIG__NSPANEL_BUTTON_MODE__FOLLOW: {
          ButtonManager::_set_relay_state(1, !current_state, true); // When button is pressed, activate relay
          break;
        }

        case NSPanelConfig__NSPanelButtonMode::NSPANEL_CONFIG__NSPANEL_BUTTON_MODE__NOTIFY_MANAGER: {
          if (!current_state) { // Only toggle on button press and not release
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
          if (!current_state) {                                                            // Only toggle on button press and not release
            ButtonManager::_set_relay_state(2, !ButtonManager::_get_relay_state(2), true); // Toggle output
          }
          break;
        }

        case NSPanelConfig__NSPanelButtonMode::NSPANEL_CONFIG__NSPANEL_BUTTON_MODE__FOLLOW: {
          ButtonManager::_set_relay_state(2, !current_state, true); // When button is pressed, activate relay
          break;
        }

        case NSPanelConfig__NSPanelButtonMode::NSPANEL_CONFIG__NSPANEL_BUTTON_MODE__NOTIFY_MANAGER: {
          if (!current_state) { // Only toggle on button press and not release
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

void ButtonManager::_set_relay_state(uint8_t relay, bool state, bool send_mqtt_update) {
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

    if (send_mqtt_update) {
      MqttManager::publish(std::format("nspanel/{}/relay1_state", WiFiManager::mac_string()), state ? "1" : "0", strlen(state ? "1" : "0"), true);

      std::shared_ptr<NSPanelConfig> config;
      if (NSPM_ConfigManager::get_config(&config) == ESP_OK) [[likely]] {
        std::string relay_group_base_topic = "nspanel/mqttmanager_";
        relay_group_base_topic.append(NSPM_ConfigManager::get_manager_address());
        relay_group_base_topic.append("/relay_groups/");
        std::string relay_group_topic;
        for (int i = 0; i < config->n_relay1_relay_group; i++) {
          relay_group_topic = relay_group_base_topic;
          relay_group_topic.append(std::to_string(config->relay1_relay_group[i]));
          relay_group_topic.append("/state");

          MqttManager::publish(relay_group_topic, state ? "1" : "0", strlen(state ? "1" : "0"), true);
        }
      }
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

    if (send_mqtt_update) {
      MqttManager::publish(std::format("nspanel/{}/relay2_state", WiFiManager::mac_string()), state ? "1" : "0", strlen(state ? "1" : "0"), true);

      std::shared_ptr<NSPanelConfig> config;
      if (NSPM_ConfigManager::get_config(&config) == ESP_OK) [[likely]] {
        std::string relay_group_base_topic = "nspanel/mqttmanager_";
        relay_group_base_topic.append(NSPM_ConfigManager::get_manager_address());
        relay_group_base_topic.append("/relay_groups/");
        std::string relay_group_topic;
        for (int i = 0; i < config->n_relay2_relay_group; i++) {
          relay_group_topic = relay_group_base_topic;
          relay_group_topic.append(std::to_string(config->relay2_relay_group[i]));
          relay_group_topic.append("/state");

          MqttManager::publish(relay_group_topic, state ? "1" : "0", strlen(state ? "1" : "0"), true);
        }
      }
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

void ButtonManager::_handle_mqtt_relay_group_topics() {
  std::vector<int32_t> current_relay1_group_ids;
  std::vector<int32_t> current_relay2_group_ids;
  if (ButtonManager::_current_config != nullptr) {
    for (int i = 0; i < ButtonManager::_current_config->n_relay1_relay_group; i++) {
      current_relay1_group_ids.push_back(ButtonManager::_current_config->relay1_relay_group[i]);
    }
    for (int i = 0; i < ButtonManager::_current_config->n_relay2_relay_group; i++) {
      current_relay2_group_ids.push_back(ButtonManager::_current_config->relay2_relay_group[i]);
    }
  }

  std::vector<int32_t> new_relay1_group_ids;
  std::vector<int32_t> new_relay2_group_ids;
  std::shared_ptr<NSPanelConfig> config;
  if (NSPM_ConfigManager::get_config(&config) == ESP_OK) [[likely]] {
    for (int i = 0; i < config->n_relay1_relay_group; i++) {
      new_relay1_group_ids.push_back(config->relay1_relay_group[i]);
    }
    for (int i = 0; i < config->n_relay2_relay_group; i++) {
      new_relay2_group_ids.push_back(config->relay2_relay_group[i]);
    }
  } else {
    ESP_LOGE("ButtonManager", "Failed to get current config while trying to subscribe/unsubscribe from relay group topics.");
    return;
  }

  // Unsubscribe from any removed relay1 group
  for (int i = 0; i < current_relay1_group_ids.size(); i++) {
    bool group_found = false;
    for (int j = 0; j < new_relay1_group_ids.size(); j++) {
      if (new_relay1_group_ids[j] == current_relay1_group_ids[i]) {
        group_found = true;
        break;
      }
    }

    if (!group_found) {
      ESP_LOGI("ButtonManager", "Removing relay group %ld from relay1 binding.", current_relay1_group_ids[i]);
      std::string relay_group_topic = "nspanel/mqttmanager_";
      relay_group_topic.append(NSPM_ConfigManager::get_manager_address());
      relay_group_topic.append("/relay_groups/");
      relay_group_topic.append(std::to_string(current_relay1_group_ids[i]));
      relay_group_topic.append("/state");
      MqttManager::unsubscribe(relay_group_topic);
    }
  }

  // Unsubscribe from any removed relay2 group
  for (int i = 0; i < current_relay2_group_ids.size(); i++) {
    bool group_found = false;
    for (int j = 0; j < new_relay2_group_ids.size(); j++) {
      if (new_relay2_group_ids[j] == current_relay2_group_ids[i]) {
        group_found = true;
        break;
      }
    }

    if (!group_found) {
      ESP_LOGI("ButtonManager", "Removing relay group %ld from relay2 binding.", current_relay2_group_ids[i]);
      std::string relay_group_topic = "nspanel/mqttmanager_";
      relay_group_topic.append(NSPM_ConfigManager::get_manager_address());
      relay_group_topic.append("/relay_groups/");
      relay_group_topic.append(std::to_string(current_relay2_group_ids[i]));
      relay_group_topic.append("/state");
      MqttManager::unsubscribe(relay_group_topic);
    }
  }

  // Subscribe to any added relay1 group
  for (int i = 0; i < new_relay1_group_ids.size(); i++) {
    ESP_LOGI("ButtonManager", "Adding relay1 to relay group ID %ld.", new_relay1_group_ids[i]);
    std::string relay_group_topic = "nspanel/mqttmanager_";
    relay_group_topic.append(NSPM_ConfigManager::get_manager_address());
    relay_group_topic.append("/relay_groups/");
    relay_group_topic.append(std::to_string(new_relay1_group_ids[i]));
    relay_group_topic.append("/state");
    MqttManager::subscribe(relay_group_topic);
  }

  // Subscribe to any added relay2 group
  for (int i = 0; i < new_relay2_group_ids.size(); i++) {
    ESP_LOGI("ButtonManager", "Adding relay2 to relay group ID %ld.", new_relay2_group_ids[i]);
    std::string relay_group_topic = "nspanel/mqttmanager_";
    relay_group_topic.append(NSPM_ConfigManager::get_manager_address());
    relay_group_topic.append("/relay_groups/");
    relay_group_topic.append(std::to_string(new_relay2_group_ids[i]));
    relay_group_topic.append("/state");
    MqttManager::subscribe(relay_group_topic);
  }
}

void ButtonManager::_nspm_configmanager_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  ButtonManager::_handle_mqtt_relay_group_topics();

  std::shared_ptr<NSPanelConfig> config;
  if (NSPM_ConfigManager::get_config(&config) == ESP_OK) [[likely]] {
    bool save = false;
    if (config->relay1_default_mode != ButtonManager::_relay1_default_mode) {
      ConfigManager::relay1_default_mode = config->relay1_default_mode;
      ButtonManager::_set_relay_state(1, config->relay1_default_mode, true);
      save = true;
    }
    if (config->relay2_default_mode != ButtonManager::_relay2_default_mode) {
      ConfigManager::relay2_default_mode = config->relay2_default_mode;
      ButtonManager::_set_relay_state(1, config->relay2_default_mode, true);
      save = true;
    }
    if (save) {
      ConfigManager::save_config();
    }

    ButtonManager::_reverse_relays = config->reverse_relays;
    ButtonManager::_button1_mode = config->button1_mode;
    ButtonManager::_button2_mode = config->button2_mode;

    ButtonManager::_current_config = config;
  } else {
    ESP_LOGE("ButtonManager", "Got new config update but ButtonManager failed to read config. Cannot update internal values.");
  }
}

void ButtonManager::_mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id == MQTT_EVENT_DATA) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    if (event->data_len == 0) {
      return;
    }

    std::string topic_string = std::string(event->topic, event->topic_len);
    std::string data = std::string(event->data, event->data_len);

    std::string relay1_topic = std::format("nspanel/{}/relay1_cmd", WiFiManager::mac_string());
    std::string relay2_topic = std::format("nspanel/{}/relay2_cmd", WiFiManager::mac_string());

    if (topic_string.compare(relay1_topic) == 0) {
      if (data.compare("0") == 0) {
        ButtonManager::_set_relay_state(1, false, true);
      } else if (data.compare("1") == 0) {
        ButtonManager::_set_relay_state(1, true, true);
      } else if (data.compare("2") == 0) {
        ButtonManager::_set_relay_state(1, !ButtonManager::_relay1_current_state, true);
      } else {
        ESP_LOGE("ButtonManager", "Got command to set relay1 state but command data was not recognized.");
      }
    } else if (topic_string.compare(relay2_topic) == 0) {
      if (data.compare("0") == 0) {
        ButtonManager::_set_relay_state(2, false, true);
      } else if (data.compare("1") == 0) {
        ButtonManager::_set_relay_state(2, true, true);
      } else if (data.compare("2") == 0) {
        ButtonManager::_set_relay_state(2, !ButtonManager::_relay2_current_state, true);
      } else {
        ESP_LOGE("ButtonManager", "Got command to set relay2 state but command data was not recognized.");
      }
    }

    // Check if it is a relay1 bound group
    std::shared_ptr<NSPanelConfig> config = ButtonManager::_current_config;
    if (config != nullptr) [[likely]] {
      std::string relay_group_base_topic = "nspanel/mqttmanager_";
      relay_group_base_topic.append(NSPM_ConfigManager::get_manager_address());
      relay_group_base_topic.append("/relay_groups/");
      std::string relay_group_topic;

      for (int i = 0; i < config->n_relay1_relay_group; i++) {
        relay_group_topic = relay_group_base_topic;
        relay_group_topic.append(std::to_string(config->relay1_relay_group[i]));
        relay_group_topic.append("/state");

        if (topic_string.compare(relay_group_topic) == 0) {
          if (data.compare("0") == 0) {
            if (ButtonManager::_get_relay_state(1)) { // Relay 1 is on, turn off.
              ESP_LOGD("ButtonManager", "Received update from relay group %ld, new state OFF.", config->relay1_relay_group[i]);
              ButtonManager::_set_relay_state(1, false, true);
            }
          } else if (data.compare("1") == 0) {
            if (!ButtonManager::_get_relay_state(1)) { // Relay 1 is off, turn off.
              ESP_LOGD("ButtonManager", "Received update from relay group %ld, new state ON.", config->relay1_relay_group[i]);
              ButtonManager::_set_relay_state(1, true, true);
            }
          } else {
            ESP_LOGE("ButtonManager", "Found matching relay group topic but failed to determine state.");
          }
        }
        break;
      }

      // Check if it is a relay2 bound group
      for (int i = 0; i < config->n_relay2_relay_group; i++) {
        relay_group_topic = relay_group_base_topic;
        relay_group_topic.append(std::to_string(config->relay2_relay_group[i]));
        relay_group_topic.append("/state");

        if (topic_string.compare(relay_group_topic) == 0) {
          if (data.compare("0") == 0) {
            if (ButtonManager::_get_relay_state(2)) { // Relay 2 is on, turn off.
              ESP_LOGD("ButtonManager", "Received update from relay group %ld, new state OFF.", config->relay2_relay_group[i]);
              ButtonManager::_set_relay_state(2, false, true);
            }
          } else if (data.compare("1") == 0) {
            if (!ButtonManager::_get_relay_state(2)) { // Relay 2 is off, turn off.
              ESP_LOGD("ButtonManager", "Received update from relay group %ld, new state ON.", config->relay2_relay_group[i]);
              ButtonManager::_set_relay_state(2, true, true);
            }
          } else {
            ESP_LOGE("ButtonManager", "Found matching relay group topic but failed to determine state.");
          }
        }
        break;
      }
    } else {
      ESP_LOGW("ButtonManager", "No config currently loaded/set. Unable to determine if received message was from a relay group. Will not change any relay state.");
    }
  } else if (event_id == MQTT_EVENT_CONNECTED) {
    ESP_LOGD("ButtonManager", "MQTT connected, resubscribing to topics.");
    ButtonManager::init_mqtt();
    ButtonManager::_handle_mqtt_relay_group_topics();
  }
}