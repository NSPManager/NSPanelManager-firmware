#include <ConfigManager.hpp>
#include <EntitiesPage.hpp>
#include <EntityPage.hpp>
#include <GUI_data.hpp>
#include <InterfaceManager.hpp>
#include <MqttManager.hpp>
#include <NSPM_ConfigManager.hpp>
#include <Nextion.hpp>
#include <Nextion_event.hpp>
#include <esp_log.h>
#include <protobuf_nspanel_entity.pb-c.h>

void EntityPage::show(std::string state_topic) {
  esp_log_level_set("EntityPage", ConfigManager::log_level);

  if (EntityPage::_current_state_mutex == NULL) {
    EntityPage::_current_state_mutex = xSemaphoreCreateMutex();
  }

  MqttManager::register_handler(MQTT_EVENT_ANY, &EntityPage::_handle_mqtt_event, NULL);

  EntityPage::_current_entity_mqtt_topic = state_topic;
  MqttManager::subscribe(EntityPage::_current_entity_mqtt_topic);
}

void EntityPage::unshow() {
  MqttManager::unregister_handler(MQTT_EVENT_ANY, &EntityPage::_handle_mqtt_event);
  esp_event_handler_unregister(NEXTION_EVENT, ESP_EVENT_ANY_ID, &EntityPage::_handle_nextion_event);
  if (!EntityPage::_current_entity_mqtt_topic.empty()) {
    MqttManager::unsubscribe(EntityPage::_current_entity_mqtt_topic);
  }
  EntityPage::_currently_showing = false;
}

void EntityPage::_handle_mqtt_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id == MQTT_EVENT_DATA) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    if (event->topic_len == 0 || event->data_len == 0) [[unlikely]] {
      return;
    }
    std::string topic_string = std::string(event->topic, event->topic_len);
    if (topic_string.compare(EntityPage::_current_entity_mqtt_topic) == 0) {
      NSPanelEntityState *new_state = nspanel_entity_state__unpack(NULL, event->data_len, (const uint8_t *)event->data);
      if (new_state != NULL) [[likely]] {
        if (xSemaphoreTake(EntityPage::_current_state_mutex, pdMS_TO_TICKS(500)) == pdPASS) [[likely]] {
          EntityPage::_current_state = std::shared_ptr<NSPanelEntityState>(new_state, &EntityPage::_delete_nspanel_entity_state_object);
          xSemaphoreGive(EntityPage::_current_state_mutex);

          // Set current display mode (RGB/Color temp) from what mode the light state is in
          switch (EntityPage::_current_state->light->current_light_mode) {
          case NSPANEL_ENTITY_STATE__LIGHT__LIGHT_MODE__COLOR_TEMP:
            EntityPage::_current_mode = _entity_page_modes::LIGHT_COLOR_TEMPERATURE;
            break;

          case NSPANEL_ENTITY_STATE__LIGHT__LIGHT_MODE__RGB:
            EntityPage::_current_mode = _entity_page_modes::LIGHT_RGB;
            break;

          default:
            ESP_LOGW("EntityPage", "Unknown light mode!");
            break;
          }

          EntityPage::_update_display();
        } else {
          ESP_LOGE("EntityPage", "Failed to take mutex to update current state.");
        }
      } else {
        ESP_LOGE("EntityPage", "Received new state but failed to parse into protobuf object.");
      }
    }
  } else if (event_id == MQTT_EVENT_CONNECTED) {
    if (!EntityPage::_current_entity_mqtt_topic.empty()) [[likely]] {
      MqttManager::subscribe(EntityPage::_current_entity_mqtt_topic);
    }
  }
}

void EntityPage::_update_display() {
  ESP_LOGD("EntityPage", "Updating displayed value via delegator.");
  switch (EntityPage::_current_state->entity_case) {
  case NSPANEL_ENTITY_STATE__ENTITY_LIGHT:
    EntityPage::_update_display_light();
    break;

  default:
    ESP_LOGE("EntityPage", "Unknown state type. Can't call appropriate update display function.");
    break;
  }
}

void EntityPage::_handle_nextion_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id == nextion_event_t::TOUCH_EVENT) {
    nextion_event_touch_t *touch_data = (nextion_event_touch_t *)event_data;
    EntityPage::_handle_touch_event(touch_data->component_id, touch_data->pressed);
  }
}

void EntityPage::_handle_config_update(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
}

void EntityPage::_handle_touch_event(uint16_t component_id, bool pressed) {
  switch (EntityPage::_current_state->entity_case) {
  case NSPANEL_ENTITY_STATE__ENTITY_LIGHT:
    EntityPage::_handle_touch_event_light(component_id, pressed);
    break;

  default:
    ESP_LOGE("EntityPage", "Unknown state type. Can't call appropriate update display function.");
    break;
  }
}

void EntityPage::_update_display_light() {
  ESP_LOGI("EntityPage", "Updating EntityPage with light state.");
  if (!EntityPage::_currently_showing) {
    ESP_LOGD("EntityPage", "Switching page to %s", GUI_LIGHT_CONTROL_PAGE::page_name);
    EntityPage::_currently_showing = true;
    if (Nextion::go_to_page(GUI_LIGHT_CONTROL_PAGE::page_name, 1000) != ESP_OK) [[unlikely]] {
      ESP_LOGE("EntityPage", "Failed to navigate Nextion to page. Will go back.");
      EntitiesPage::show(EntitiesPage::display_type_t::ENTITIES);
    }

    InterfaceManager::call_unshow_callback();
    InterfaceManager::current_page_unshow_callback.set(EntityPage::unshow);

    esp_event_handler_register(NEXTION_EVENT, ESP_EVENT_ANY_ID, &EntityPage::_handle_nextion_event, NULL);
  }

  std::shared_ptr<NSPanelEntityState> state = EntityPage::_current_state;

  // Show button to switch modes IF light can both color and color temp
  if (state->light->can_color && state->light->can_color_temp) {
    Nextion::set_component_visibility(GUI_LIGHT_CONTROL_PAGE::switch_mode_button_name, true, 1000);
  } else {
    Nextion::set_component_visibility(GUI_LIGHT_CONTROL_PAGE::switch_mode_button_name, false, 1000);
  }

  // Update name if it has changed
  if (EntityPage::_last_displayed_name.compare(state->light->name) != 0) {
    Nextion::set_component_text(GUI_LIGHT_CONTROL_PAGE::light_label_name, state->light->name, 500);
    EntityPage::_last_displayed_name = state->light->name;
  }

  // Update brightness if it has changed
  if (EntityPage::_last_brightness != state->light->brightness) {
    Nextion::set_component_value(GUI_LIGHT_CONTROL_PAGE::brightness_slider_name, state->light->brightness, 250);
    EntityPage::_last_brightness = state->light->brightness;
  }

  // Update hue if it has changed
  if (EntityPage::_last_hue != state->light->hue) {
    Nextion::set_component_value(GUI_LIGHT_CONTROL_PAGE::hue_slider_name, state->light->hue, 250);
    EntityPage::_last_hue = state->light->hue;
  }

  // Set component icons and visibility
  if (EntityPage::_current_mode == EntityPage::_entity_page_modes::LIGHT_COLOR_TEMPERATURE) {
    Nextion::set_component_pic(GUI_LIGHT_CONTROL_PAGE::kelvin_saturation_slider_name, GUI_LIGHT_CONTROL_PAGE::kelvin_slider_pic, 250);
    Nextion::set_component_pic1(GUI_LIGHT_CONTROL_PAGE::kelvin_saturation_slider_name, GUI_LIGHT_CONTROL_PAGE::kelvin_slider_pic1, 250);

    Nextion::set_component_pic(GUI_LIGHT_CONTROL_PAGE::switch_mode_button_name, GUI_LIGHT_CONTROL_PAGE::rgb_mode_pic, 250); // Indicate that the user can switch to RGB-mode by pressing the button
    Nextion::set_component_visibility(GUI_LIGHT_CONTROL_PAGE::hue_slider_name, false, 250);
  } else if (EntityPage::_current_mode == EntityPage::_entity_page_modes::LIGHT_RGB) {
    Nextion::set_component_pic(GUI_LIGHT_CONTROL_PAGE::kelvin_saturation_slider_name, GUI_LIGHT_CONTROL_PAGE::saturation_slider_pic, 250);
    Nextion::set_component_pic1(GUI_LIGHT_CONTROL_PAGE::kelvin_saturation_slider_name, GUI_LIGHT_CONTROL_PAGE::saturation_slider_pic1, 250);

    Nextion::set_component_pic(GUI_LIGHT_CONTROL_PAGE::switch_mode_button_name, GUI_LIGHT_CONTROL_PAGE::kelvin_mode_pic, 250); // Indicate that the user can switch to color temperature mode by pressing the button
    Nextion::set_component_visibility(GUI_LIGHT_CONTROL_PAGE::hue_slider_name, true, 250);
  } else {
    ESP_LOGE("EntityPage", "Unknown light mode!");
  }

  if (state->light->can_color && state->light->can_color_temp) {
    if (EntityPage::_current_mode == _entity_page_modes::LIGHT_COLOR_TEMPERATURE) {
      if (EntityPage::_last_kelvin_pct != state->light->can_color_temp) {
        Nextion::set_component_value(GUI_LIGHT_CONTROL_PAGE::kelvin_saturation_slider_name, state->light->color_temp, 250);
        EntityPage::_last_saturation_pct = state->light->color_temp;
      }
      Nextion::set_component_visibility(GUI_LIGHT_CONTROL_PAGE::hue_slider_name, false, 250);
      Nextion::set_component_visibility(GUI_LIGHT_CONTROL_PAGE::kelvin_saturation_slider_name, true, 250);
    } else if (EntityPage::_current_mode == _entity_page_modes::LIGHT_RGB) {
      if (EntityPage::_last_saturation_pct != state->light->saturation) {
        Nextion::set_component_value(GUI_LIGHT_CONTROL_PAGE::kelvin_saturation_slider_name, state->light->saturation, 250);
        EntityPage::_last_saturation_pct = state->light->saturation;
      }
      Nextion::set_component_visibility(GUI_LIGHT_CONTROL_PAGE::hue_slider_name, true, 250);
      Nextion::set_component_visibility(GUI_LIGHT_CONTROL_PAGE::kelvin_saturation_slider_name, true, 250);
    } else {
      ESP_LOGE("EntityPage", "Unknown color mode for light!");
    }
  } else if (state->light->can_color_temp) {
    EntityPage::_current_mode = _entity_page_modes::LIGHT_COLOR_TEMPERATURE;
    Nextion::set_component_value(GUI_LIGHT_CONTROL_PAGE::kelvin_saturation_slider_name, state->light->color_temp, 250);
    EntityPage::_last_saturation_pct = state->light->color_temp;
    Nextion::set_component_visibility(GUI_LIGHT_CONTROL_PAGE::hue_slider_name, false, 250);
    Nextion::set_component_visibility(GUI_LIGHT_CONTROL_PAGE::kelvin_saturation_slider_name, true, 250);
  } else if (state->light->can_color) {
    EntityPage::_current_mode = _entity_page_modes::LIGHT_RGB;
    Nextion::set_component_value(GUI_LIGHT_CONTROL_PAGE::kelvin_saturation_slider_name, state->light->saturation, 250);
    EntityPage::_last_saturation_pct = state->light->saturation;
    Nextion::set_component_visibility(GUI_LIGHT_CONTROL_PAGE::kelvin_saturation_slider_name, true, 250);
    Nextion::set_component_visibility(GUI_LIGHT_CONTROL_PAGE::hue_slider_name, true, 250);
  } else {
    ESP_LOGD("EntityPage", "Light is not capable of color or color temp, will hide kelvin/saturation slider.");
    Nextion::set_component_visibility(GUI_LIGHT_CONTROL_PAGE::kelvin_saturation_slider_name, false, 250);
  }
}

void EntityPage::_handle_touch_event_light(uint16_t component_id, bool pressed) {
  ESP_LOGD("EntityPage", "Touch component %d, pressed %s", component_id, pressed ? "Yes" : "No");
  switch (component_id) {
  case GUI_LIGHT_CONTROL_PAGE::back_button_id:
    ESP_LOGD("EntityPage", "Received touch event to go back.");
    EntitiesPage::show(EntitiesPage::display_type_t::ENTITIES);
    break;

  case GUI_LIGHT_CONTROL_PAGE::switch_mode_button_id: {
    if (EntityPage::_current_mode == EntityPage::_entity_page_modes::LIGHT_COLOR_TEMPERATURE) {
      EntityPage::_current_mode = EntityPage::_entity_page_modes::LIGHT_RGB;
    } else {
      EntityPage::_current_mode = EntityPage::_entity_page_modes::LIGHT_COLOR_TEMPERATURE;
    }
    EntityPage::_update_display_light();
    break;
  }

  case GUI_LIGHT_CONTROL_PAGE::brightness_slider_id: {
    int32_t new_brightness;
    int32_t light_id = EntityPage::_current_state->light->light_id;
    if (Nextion::get_component_integer_value(GUI_LIGHT_CONTROL_PAGE::brightness_slider_name, &new_brightness, 250, 250) == ESP_OK) [[likely]] {
      NSPanelMQTTManagerCommand__LightCommand light_command = NSPANEL_MQTTMANAGER_COMMAND__LIGHT_COMMAND__INIT;
      light_command.light_ids = &light_id;
      light_command.n_light_ids = 1;
      light_command.has_brightness = true;
      light_command.brightness = new_brightness;

      NSPanelMQTTManagerCommand cmd = NSPANEL_MQTTMANAGER_COMMAND__INIT;
      cmd.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_LIGHT_COMMAND;
      cmd.light_command = &light_command;

      uint32_t packed_length = nspanel_mqttmanager_command__get_packed_size(&cmd);
      std::vector<uint8_t> buffer(packed_length); // Use vector for automatic cleanup of data when going out of scope
      size_t packed_data_size = nspanel_mqttmanager_command__pack(&cmd, buffer.data());
      if (packed_data_size == packed_length) [[likely]] {
        if (MqttManager::publish(NSPM_ConfigManager::get_manager_command_topic(), (const char *)buffer.data(), packed_length, false) == ESP_OK) [[likely]] {
          EntityPage::_last_brightness = new_brightness;
        } else {
          ESP_LOGE("EntityPage", "Failed to send MQTT message with command payload.");
          EntityPage::_update_display_light(); // Update display to reset values to those stored
        }
      } else {
        ESP_LOGE("EntityPage", "Failed to pack protobuf command.");
        EntityPage::_update_display_light(); // Update display to reset values to those stored
      }

    } else {
      ESP_LOGE("EntityPage", "Failed to get new brightness value from Nextion. Will not send update command.");
      EntityPage::_update_display_light(); // Update display to reset values to those stored
    }
    break;
  }

  case GUI_LIGHT_CONTROL_PAGE::kelvin_saturation_slider_id: {
    int32_t new_kelvin_saturation;
    int32_t light_id = EntityPage::_current_state->light->light_id;
    if (Nextion::get_component_integer_value(GUI_LIGHT_CONTROL_PAGE::kelvin_saturation_slider_name, &new_kelvin_saturation, 250, 250) == ESP_OK) [[likely]] {
      NSPanelMQTTManagerCommand__LightCommand light_command = NSPANEL_MQTTMANAGER_COMMAND__LIGHT_COMMAND__INIT;
      light_command.light_ids = &light_id;
      light_command.n_light_ids = 1;
      if (EntityPage::_current_mode == EntityPage::_entity_page_modes::LIGHT_COLOR_TEMPERATURE) {
        light_command.has_color_temperature = true;
        light_command.color_temperature = new_kelvin_saturation;
      } else if (EntityPage::_current_mode == EntityPage::_entity_page_modes::LIGHT_RGB) {
        light_command.has_saturation = true;
        light_command.saturation = new_kelvin_saturation;
      }

      NSPanelMQTTManagerCommand cmd = NSPANEL_MQTTMANAGER_COMMAND__INIT;
      cmd.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_LIGHT_COMMAND;
      cmd.light_command = &light_command;

      uint32_t packed_length = nspanel_mqttmanager_command__get_packed_size(&cmd);
      std::vector<uint8_t> buffer(packed_length); // Use vector for automatic cleanup of data when going out of scope
      size_t packed_data_size = nspanel_mqttmanager_command__pack(&cmd, buffer.data());
      if (packed_data_size == packed_length) [[likely]] {
        if (MqttManager::publish(NSPM_ConfigManager::get_manager_command_topic(), (const char *)buffer.data(), packed_length, false) == ESP_OK) [[likely]] {
          if (EntityPage::_current_mode == EntityPage::_entity_page_modes::LIGHT_COLOR_TEMPERATURE) {
            EntityPage::_last_kelvin_pct = new_kelvin_saturation;
          } else if (EntityPage::_current_mode == EntityPage::_entity_page_modes::LIGHT_RGB) {
            EntityPage::_last_saturation_pct = new_kelvin_saturation;
          }
        } else {
          ESP_LOGE("EntityPage", "Failed to send MQTT message with command payload.");
          EntityPage::_update_display_light(); // Update display to reset values to those stored
        }
      } else {
        ESP_LOGE("EntityPage", "Failed to pack protobuf command.");
        EntityPage::_update_display_light(); // Update display to reset values to those stored
      }

    } else {
      ESP_LOGE("EntityPage", "Failed to get new brightness value from Nextion. Will not send update command.");
      EntityPage::_update_display_light(); // Update display to reset values to those stored
    }
    break;
  }

  case GUI_LIGHT_CONTROL_PAGE::hue_slider_id: {
    int32_t new_hue;
    int32_t light_id = EntityPage::_current_state->light->light_id;
    if (Nextion::get_component_integer_value(GUI_LIGHT_CONTROL_PAGE::hue_slider_name, &new_hue, 250, 250) == ESP_OK) [[likely]] {
      NSPanelMQTTManagerCommand__LightCommand light_command = NSPANEL_MQTTMANAGER_COMMAND__LIGHT_COMMAND__INIT;
      light_command.light_ids = &light_id;
      light_command.n_light_ids = 1;
      light_command.has_hue = true;
      light_command.hue = new_hue;

      NSPanelMQTTManagerCommand cmd = NSPANEL_MQTTMANAGER_COMMAND__INIT;
      cmd.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_LIGHT_COMMAND;
      cmd.light_command = &light_command;

      uint32_t packed_length = nspanel_mqttmanager_command__get_packed_size(&cmd);
      std::vector<uint8_t> buffer(packed_length); // Use vector for automatic cleanup of data when going out of scope
      size_t packed_data_size = nspanel_mqttmanager_command__pack(&cmd, buffer.data());
      if (packed_data_size == packed_length) [[likely]] {
        if (MqttManager::publish(NSPM_ConfigManager::get_manager_command_topic(), (const char *)buffer.data(), packed_length, false) == ESP_OK) [[likely]] {
          EntityPage::_last_hue = new_hue;
        } else {
          ESP_LOGE("EntityPage", "Failed to send MQTT message with command payload.");
          EntityPage::_update_display_light(); // Update display to reset values to those stored
        }
      } else {
        ESP_LOGE("EntityPage", "Failed to pack protobuf command.");
        EntityPage::_update_display_light(); // Update display to reset values to those stored
      }

    } else {
      ESP_LOGE("EntityPage", "Failed to get new brightness value from Nextion. Will not send update command.");
      EntityPage::_update_display_light(); // Update display to reset values to those stored
    }
    break;
  }

  default:
    break;
  }
}

void EntityPage::_delete_nspanel_entity_state_object(NSPanelEntityState *object) {
  nspanel_entity_state__free_unpacked(object, NULL);
}