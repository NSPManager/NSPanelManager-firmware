#include <AlbumArt.hpp>
#include <ConfigManager.hpp>
#include <EntitiesPage.hpp>
#include <EntityPage.hpp>
#include <GUI_data.hpp>
#include <InterfaceManager.hpp>
#include <MediaSourcePage.hpp>
#include <MqttManager.hpp>
#include <NSPM_ConfigManager.hpp>
#include <Nextion.hpp>
#include <Nextion_event.hpp>
#include <StatusUpdateManager.hpp>
#include <algorithm>
#include <esp_log.h>
#include <esp_timer.h>
#include <protobuf_nspanel_entity.pb-c.h>

EntityPage::_media_player_component_t EntityPage::_media_player_components[] = {
    {GUI_MEDIA_PLAYER_CONTROL_PAGE::back_button_name, _media_player_action::BACK, GUI_MEDIA_PLAYER_CONTROL_PAGE::back_button_id},
    {GUI_MEDIA_PLAYER_CONTROL_PAGE::previous_track_button_name, _media_player_action::PREVIOUS_TRACK, GUI_MEDIA_PLAYER_CONTROL_PAGE::previous_track_button_id},
    {GUI_MEDIA_PLAYER_CONTROL_PAGE::next_track_button_name, _media_player_action::NEXT_TRACK, GUI_MEDIA_PLAYER_CONTROL_PAGE::next_track_button_id},
    {GUI_MEDIA_PLAYER_CONTROL_PAGE::play_pause_button_name, _media_player_action::PLAY_PAUSE, GUI_MEDIA_PLAYER_CONTROL_PAGE::play_pause_button_id},
    {GUI_MEDIA_PLAYER_CONTROL_PAGE::mute_button_name, _media_player_action::MUTE, GUI_MEDIA_PLAYER_CONTROL_PAGE::mute_button_id},
    {GUI_MEDIA_PLAYER_CONTROL_PAGE::source_switch_button_name, _media_player_action::SOURCE_SWITCH, GUI_MEDIA_PLAYER_CONTROL_PAGE::source_switch_button_id},
    {GUI_MEDIA_PLAYER_CONTROL_PAGE::volume_up_button_name, _media_player_action::VOLUME_UP, GUI_MEDIA_PLAYER_CONTROL_PAGE::volume_up_button_id},
    {GUI_MEDIA_PLAYER_CONTROL_PAGE::volume_down_button_name, _media_player_action::VOLUME_DOWN, GUI_MEDIA_PLAYER_CONTROL_PAGE::volume_down_button_id},
    {GUI_MEDIA_PLAYER_CONTROL_PAGE::album_art_hotspot_name, _media_player_action::ALBUM_ART, GUI_MEDIA_PLAYER_CONTROL_PAGE::album_art_hotspot_id},
};

void EntityPage::show(std::string state_topic) {
  esp_log_level_set("EntityPage", ConfigManager::log_level);

  if (EntityPage::_current_state_mutex == NULL) {
    EntityPage::_current_state_mutex = xSemaphoreCreateMutex();
  }

  MqttManager::register_handler(MQTT_EVENT_ANY, &EntityPage::_handle_mqtt_event, NULL);

  EntityPage::_current_entity_mqtt_topic = state_topic;
  MqttManager::subscribe(EntityPage::_current_entity_mqtt_topic);

  // Reset variables:
  EntityPage::_selected_thermostat_option_index = 0;
  EntityPage::_is_currently_editing = false;
}

void EntityPage::return_to_media_player(std::string state_topic) {
  EntityPage::_media_player_returning = true;
  // Subscribing to the state topic again delivers the retained state, which opens the media player page.
  EntityPage::show(state_topic);
}

void EntityPage::unshow() {
  MqttManager::unregister_handler(MQTT_EVENT_ANY, &EntityPage::_handle_mqtt_event);
  esp_event_handler_unregister(NEXTION_EVENT, ESP_EVENT_ANY_ID, &EntityPage::_handle_nextion_event);
  if (!EntityPage::_current_entity_mqtt_topic.empty()) {
    MqttManager::unsubscribe(EntityPage::_current_entity_mqtt_topic);
  }
  EntityPage::_currently_showing = false;
  // A full render takes seconds and outlives this page, so stop it rather than let it paint
  // over whatever comes next.
  AlbumArt::cancel();
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
          auto state = EntityPage::_get_current_state();

          // Set current display mode (RGB/Color temp) from what mode the light state is in
          if (state->entity_case == NSPanelEntityState__EntityCase::NSPANEL_ENTITY_STATE__ENTITY_LIGHT) {
            switch (state->light->current_light_mode) {
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
          } else if (state->entity_case == NSPanelEntityState__EntityCase::NSPANEL_ENTITY_STATE__ENTITY_THERMOSTAT) {
            EntityPage::_current_mode = _entity_page_modes::THERMOSTAT;
          } else if (state->entity_case == NSPanelEntityState__EntityCase::NSPANEL_ENTITY_STATE__ENTITY_MEDIA_PLAYER) {
            EntityPage::_current_mode = _entity_page_modes::MEDIA_PLAYER;
            EntityPage::_log_media_player_state(state->media_player);
            EntityPage::_media_player_confirm_pending_volume(state->media_player);
          } else {
            ESP_LOGE("EntityPage", "Unknown entity state case!");
          }

          EntityPage::_update_display();
        } else {
          ESP_LOGE("EntityPage", "Failed to take mutex to update current state.");
          nspanel_entity_state__free_unpacked(new_state, NULL);
        }
      } else {
        ESP_LOGE("EntityPage", "Received new state but failed to parse into protobuf object.");
      }
    }
  }
}

void EntityPage::_update_display() {
  ESP_LOGD("EntityPage", "Updating displayed value via delegator.");
  std::shared_ptr<NSPanelEntityState> state = EntityPage::_get_current_state();
  if (state == nullptr) [[unlikely]] {
    return;
  }

  switch (state->entity_case) {
  case NSPANEL_ENTITY_STATE__ENTITY_LIGHT:
    EntityPage::_update_display_light();
    break;

  case NSPANEL_ENTITY_STATE__ENTITY_THERMOSTAT:
    EntityPage::_update_display_thermostat();
    break;

  case NSPANEL_ENTITY_STATE__ENTITY_MEDIA_PLAYER:
    EntityPage::_update_display_media_player();
    break;

  default:
    ESP_LOGE("EntityPage", "Unknown state type. Can't call appropriate update display function.");
    break;
  }
}

void EntityPage::_handle_nextion_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  // Take our own reference for the whole event. If we went through _current_state directly, nothing would
  // keep the state alive when a new state arrives on the MQTT task and replaces it mid-handler.
  std::shared_ptr<NSPanelEntityState> state = EntityPage::_get_current_state();
  if (state == nullptr) { // No state received yet. Nothing to act on.
    return;
  }

  if (event_id == nextion_event_t::TOUCH_EVENT) {
    nextion_event_touch_t *touch_data = (nextion_event_touch_t *)event_data;
    EntityPage::_handle_touch_event(state, touch_data->component_id, touch_data->pressed);
  } else if (event_id == nextion_event_t::STRING_EVENT) {
    switch (state->entity_case) {
    case NSPANEL_ENTITY_STATE__ENTITY_LIGHT:
      break;

    case NSPANEL_ENTITY_STATE__ENTITY_THERMOSTAT:
      EntityPage::_handle_string_event_thermostat(state, (char *)event_data);
      break;

    case NSPANEL_ENTITY_STATE__ENTITY_MEDIA_PLAYER:
      break;

    default:
      ESP_LOGE("EntityPage", "Unknown state type. Can't call appropriate string event function.");
      break;
    }
  }
}

void EntityPage::_handle_config_update(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
}

void EntityPage::_handle_touch_event(const std::shared_ptr<NSPanelEntityState> &state, uint16_t component_id, bool pressed) {
  switch (state->entity_case) {
  case NSPANEL_ENTITY_STATE__ENTITY_LIGHT:
    EntityPage::_handle_touch_event_light(state, component_id, pressed);
    break;

  case NSPANEL_ENTITY_STATE__ENTITY_THERMOSTAT:
    break;

  case NSPANEL_ENTITY_STATE__ENTITY_MEDIA_PLAYER:
    EntityPage::_handle_touch_event_media_player(component_id, pressed);
    break;

  default:
    ESP_LOGE("EntityPage", "Unknown state type. Can't call appropriate touch function.");
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
      return;
    }

    InterfaceManager::call_unshow_callback();
    InterfaceManager::current_page_unshow_callback.set(EntityPage::unshow);

    esp_event_handler_register(NEXTION_EVENT, ESP_EVENT_ANY_ID, &EntityPage::_handle_nextion_event, NULL);
  }

  auto state = EntityPage::_get_current_state();

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

void EntityPage::_handle_touch_event_light(const std::shared_ptr<NSPanelEntityState> &state, uint16_t component_id, bool pressed) {
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
    int32_t light_id = state->light->light_id;
    if (Nextion::get_component_integer_value(GUI_LIGHT_CONTROL_PAGE::brightness_slider_name, &new_brightness, 250, 250) == ESP_OK) [[likely]] {
      NSPanelMQTTManagerCommand__LightCommand light_command = NSPANEL_MQTTMANAGER_COMMAND__LIGHT_COMMAND__INIT;
      light_command.light_ids = &light_id;
      light_command.n_light_ids = 1;
      light_command.has_brightness = true;
      light_command.brightness = new_brightness;

      NSPanelMQTTManagerCommand cmd = NSPANEL_MQTTMANAGER_COMMAND__INIT;
      cmd.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_LIGHT_COMMAND;
      cmd.light_command = &light_command;
      cmd.nspanel_id = NSPM_ConfigManager::get_nspanel_id();

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
    int32_t light_id = state->light->light_id;
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
      cmd.nspanel_id = NSPM_ConfigManager::get_nspanel_id();

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
    int32_t light_id = state->light->light_id;
    if (Nextion::get_component_integer_value(GUI_LIGHT_CONTROL_PAGE::hue_slider_name, &new_hue, 250, 250) == ESP_OK) [[likely]] {
      NSPanelMQTTManagerCommand__LightCommand light_command = NSPANEL_MQTTMANAGER_COMMAND__LIGHT_COMMAND__INIT;
      light_command.light_ids = &light_id;
      light_command.n_light_ids = 1;
      light_command.has_hue = true;
      light_command.hue = new_hue;

      NSPanelMQTTManagerCommand cmd = NSPANEL_MQTTMANAGER_COMMAND__INIT;
      cmd.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_LIGHT_COMMAND;
      cmd.light_command = &light_command;
      cmd.nspanel_id = NSPM_ConfigManager::get_nspanel_id();

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

void EntityPage::_update_display_thermostat() {
  ESP_LOGI("EntityPage", "Updating EntityPage with thermostat state.");
  if (!EntityPage::_currently_showing) {
    ESP_LOGD("EntityPage", "Switching page to %s", GUI_THERMOSTAT_CONTROL_PAGE::page_name);
    EntityPage::_currently_showing = true;
    if (Nextion::go_to_page(GUI_THERMOSTAT_CONTROL_PAGE::page_name, 1000) != ESP_OK) [[unlikely]] {
      ESP_LOGE("EntityPage", "Failed to navigate Nextion to page. Will go back.");
      EntitiesPage::show(EntitiesPage::display_type_t::ENTITIES);
    }

    InterfaceManager::call_unshow_callback();
    InterfaceManager::current_page_unshow_callback.set(EntityPage::unshow);

    esp_event_handler_register(NEXTION_EVENT, ESP_EVENT_ANY_ID, &EntityPage::_handle_nextion_event, NULL);
  }

  std::shared_ptr<NSPanelEntityState> state = EntityPage::_get_current_state();

  // Loop over all options. Set them to the corresponding value if an option in the index
  // is available in the state data. If not, clean it and hide it.
  for (int i = 0; i < sizeof(GUI_THERMOSTAT_CONTROL_PAGE::options) / sizeof(GUI_THERMOSTAT_OPTIONS_MODE_DATA); i++) {
    if (i < state->thermostat->n_options) {
      Nextion::set_component_visibility(GUI_THERMOSTAT_CONTROL_PAGE::options[i].icon_name, true, 1000);
      Nextion::set_component_visibility(GUI_THERMOSTAT_CONTROL_PAGE::options[i].label_name, true, 1000);
      Nextion::set_component_text(GUI_THERMOSTAT_CONTROL_PAGE::options[i].icon_name, state->thermostat->options[i]->current_icon, 1000);
      Nextion::set_component_text(GUI_THERMOSTAT_CONTROL_PAGE::options[i].label_name, state->thermostat->options[i]->name, 1000);
    } else {
      Nextion::set_component_visibility(GUI_THERMOSTAT_CONTROL_PAGE::options[i].icon_name, false, 1000);
      Nextion::set_component_visibility(GUI_THERMOSTAT_CONTROL_PAGE::options[i].label_name, false, 1000);
    }
  }

  char buf[16];
  uint8_t chars_written = snprintf(buf, sizeof(buf), "%.1f°", state->thermostat->set_temperature);
  if (chars_written > 0) {
    if (!EntityPage::_is_currently_editing) { // Only update displayed temperature in case we are currently not editing any other option.
      Nextion::set_component_text(GUI_THERMOSTAT_CONTROL_PAGE::set_label_name, buf, 1000);
    }
  } else {
    ESP_LOGE("EntityPage", "Failed to snprintf set temp to temperature buffer.");
  }

  Nextion::set_component_text(GUI_THERMOSTAT_CONTROL_PAGE::room_name_label_name, state->thermostat->name, 1000);
  chars_written = 0;
  if (state->thermostat->has_current_temperature) {
    chars_written = snprintf(buf, sizeof(buf), "%.1f°", state->thermostat->current_temperature);
  } else {
    ESP_LOGI("EntityPage", "Thermostat does not have a valid temperature for location. Will use internal sensor.");
    chars_written = snprintf(buf, sizeof(buf), "%.1f°", StatusUpdateManager::current_temperature());
  }

  if (chars_written > 0) {
    Nextion::set_component_text(GUI_THERMOSTAT_CONTROL_PAGE::temperature_label_name, buf, 1000);
  } else {
    ESP_LOGE("EntityPage", "Failed to snprintf current temp to temperature buffer.");
  }
}

void EntityPage::_handle_string_event_thermostat(const std::shared_ptr<NSPanelEntityState> &state, char *data) {
  ESP_LOGD("EntityPage", "Thermostat page received string event data: %s", data);

  if (strcmp(data, "activate:set1") == 0) {
    if (state->thermostat->n_options >= 1) {
      EntityPage::_send_thermostat_option_command();
      ESP_LOGD("EntityPage", "Activating thermostat options set1");
      EntityPage::_is_currently_editing = true;
      EntityPage::_selected_thermostat_option_index = 0;
      Nextion::set_component_text(GUI_THERMOSTAT_CONTROL_PAGE::set_label_name, state->thermostat->options[0]->current_value, 1000);
    }
  } else if (strcmp(data, "activate:set2") == 0) {
    if (state->thermostat->n_options >= 2) {
      EntityPage::_send_thermostat_option_command();
      ESP_LOGD("EntityPage", "Activating thermostat options set2");
      EntityPage::_is_currently_editing = true;
      EntityPage::_selected_thermostat_option_index = 1;
      Nextion::set_component_text(GUI_THERMOSTAT_CONTROL_PAGE::set_label_name, state->thermostat->options[1]->current_value, 1000);
    }
  } else if (strcmp(data, "activate:set3") == 0) {
    if (state->thermostat->n_options >= 3) {
      EntityPage::_send_thermostat_option_command();
      ESP_LOGD("EntityPage", "Activating thermostat options set3");
      EntityPage::_is_currently_editing = true;
      EntityPage::_selected_thermostat_option_index = 2;
      Nextion::set_component_text(GUI_THERMOSTAT_CONTROL_PAGE::set_label_name, state->thermostat->options[2]->current_value, 1000);
    }
  } else if (strcmp(data, "activate:set4") == 0) {
    if (state->thermostat->n_options >= 4) {
      EntityPage::_send_thermostat_option_command();
      ESP_LOGD("EntityPage", "Activating thermostat options set4");
      EntityPage::_is_currently_editing = true;
      EntityPage::_selected_thermostat_option_index = 3;
      Nextion::set_component_text(GUI_THERMOSTAT_CONTROL_PAGE::set_label_name, state->thermostat->options[3]->current_value, 1000);
    }
  } else if (strcmp(data, "activate:set5") == 0) {
    if (state->thermostat->n_options >= 5) {
      EntityPage::_send_thermostat_option_command();
      ESP_LOGD("EntityPage", "Activating thermostat options set5");
      EntityPage::_is_currently_editing = true;
      EntityPage::_selected_thermostat_option_index = 4;
      Nextion::set_component_text(GUI_THERMOSTAT_CONTROL_PAGE::set_label_name, state->thermostat->options[4]->current_value, 1000);
    }
  } else if (strcmp(data, "deactivate") == 0) { // We've edited an option. Send updated state to manager.
    EntityPage::_send_thermostat_option_command();
    EntityPage::_is_currently_editing = false;
    EntityPage::_update_display_thermostat();
  } else if (strcmp(data, "deactivatetemp") == 0) { // We've changed the temperature. Send updated temperature to manager.
    EntityPage::_send_thermostat_setpoint_command();
  } else if (strcmp(data, "back") == 0) {
    EntitiesPage::show(EntitiesPage::display_type_t::ENTITIES);

    if (EntityPage::_is_currently_editing) [[unlikely]] { // We are currently in editing mode, send the current option.
      EntityPage::_send_thermostat_option_command();
    } else {
      EntityPage::_send_thermostat_setpoint_command();
    }
  } else if (strcmp(data, "tempup") == 0) {
    if (EntityPage::_is_currently_editing) {
      if (EntityPage::_selected_thermostat_option_index < state->thermostat->n_options) {
        if (state->thermostat->options[EntityPage::_selected_thermostat_option_index]->n_options <= 0) {
          return; // Do nothing, there is no options available.
        }

        // Find current index of currently selected option
        uint8_t current_index = 0;
        for (int i = 0; i < state->thermostat->options[EntityPage::_selected_thermostat_option_index]->n_options; i++) {
          if (strcmp(state->thermostat->options[EntityPage::_selected_thermostat_option_index]->options[i]->value, state->thermostat->options[EntityPage::_selected_thermostat_option_index]->current_value) == 0) {
            current_index = i;
            break;
          }
        }

        current_index++;
        if (current_index >= state->thermostat->options[EntityPage::_selected_thermostat_option_index]->n_options) {
          current_index = 0; // Reset index to 0 to restart loop of options
        }

        bool alloc_failed = false;
        uint16_t new_value_len = strlen(state->thermostat->options[EntityPage::_selected_thermostat_option_index]->options[current_index]->value);
        uint16_t new_icon_len = strlen(state->thermostat->options[EntityPage::_selected_thermostat_option_index]->options[current_index]->icon);

        char *new_value = (char *)malloc(new_value_len + 1);
        char *new_icon = (char *)malloc(new_icon_len + 1);

        if (new_value == NULL) {
          alloc_failed = true;
          ESP_LOGE("EntityPage", "Failed to allocate memory for new value. New value length: %u", new_value_len);
          if (new_icon != NULL) {
            free(new_icon); // Free new icon as it won't be used when alloc failed.
          }
        }

        if (new_icon == NULL) {
          alloc_failed = true;
          ESP_LOGE("EntityPage", "Failed to allocate memory for new icon. New icon length: %u", new_value_len);
          free(new_value); // Free new value as it won't be used when alloc failed.
        }

        if (!alloc_failed) [[likely]] {
          strncpy(new_value, state->thermostat->options[EntityPage::_selected_thermostat_option_index]->options[current_index]->value, new_value_len);
          strncpy(new_icon, state->thermostat->options[EntityPage::_selected_thermostat_option_index]->options[current_index]->icon, new_icon_len);
          new_value[new_value_len] = '\0';
          new_icon[new_icon_len] = '\0';

          taskENTER_CRITICAL(&EntityPage::_entity_page_spinlock);
          if (new_value_len != 0) {
            free(state->thermostat->options[EntityPage::_selected_thermostat_option_index]->current_value);
            state->thermostat->options[EntityPage::_selected_thermostat_option_index]->current_value = new_value;
          }

          if (new_icon_len != 0) {
            free(state->thermostat->options[EntityPage::_selected_thermostat_option_index]->current_icon);
            state->thermostat->options[EntityPage::_selected_thermostat_option_index]->current_icon = new_icon;
          }
          taskEXIT_CRITICAL(&EntityPage::_entity_page_spinlock);
        }

        Nextion::set_component_text(GUI_THERMOSTAT_CONTROL_PAGE::set_label_name, state->thermostat->options[EntityPage::_selected_thermostat_option_index]->current_value, 1000);
        Nextion::set_component_text(GUI_THERMOSTAT_CONTROL_PAGE::options[EntityPage::_selected_thermostat_option_index].icon_name, state->thermostat->options[EntityPage::_selected_thermostat_option_index]->current_icon, 1000);
      }
    } else {
      state->thermostat->set_temperature += state->thermostat->step_size;
      EntityPage::_update_display_thermostat();
    }
  } else if (strcmp(data, "tempdown") == 0) {
    if (EntityPage::_is_currently_editing) {
      if (EntityPage::_selected_thermostat_option_index < state->thermostat->n_options) {
        if (state->thermostat->options[EntityPage::_selected_thermostat_option_index]->n_options <= 0) {
          return; // Do nothing, there is no options available.
        }

        // Find current index of currently selected option
        uint8_t current_index = 0;
        for (int i = state->thermostat->options[EntityPage::_selected_thermostat_option_index]->n_options - 1; i > 0; i--) {
          if (strcmp(state->thermostat->options[EntityPage::_selected_thermostat_option_index]->options[i]->value, state->thermostat->options[EntityPage::_selected_thermostat_option_index]->current_value) == 0) {
            current_index = i;
            break;
          }
        }

        current_index--;
        if (current_index == 255) {                                                                                 // We've loop all the way around.
          current_index = state->thermostat->options[EntityPage::_selected_thermostat_option_index]->n_options - 1; // Reset index to last item to restart loop of options
        }
        bool alloc_failed = false;
        uint16_t new_value_len = strlen(state->thermostat->options[EntityPage::_selected_thermostat_option_index]->options[current_index]->value);
        uint16_t new_icon_len = strlen(state->thermostat->options[EntityPage::_selected_thermostat_option_index]->options[current_index]->icon);
        char *new_value = (char *)malloc(new_value_len + 1);
        char *new_icon = (char *)malloc(new_icon_len + 1);

        if (new_value == NULL) {
          alloc_failed = true;
          ESP_LOGE("EntityPage", "Failed to allocate memory for new value. New value length: %u", new_value_len);
          if (new_icon != NULL) {
            free(new_icon); // Free new icon as it won't be used when alloc failed.
          }
        } else if (new_icon == NULL) {
          alloc_failed = true;
          ESP_LOGE("EntityPage", "Failed to allocate memory for new icon. New icon length: %u", new_value_len);
          free(new_value); // Free new value as it won't be used when alloc failed.
        }

        if (!alloc_failed) [[likely]] {
          strncpy(new_value, state->thermostat->options[EntityPage::_selected_thermostat_option_index]->options[current_index]->value, new_value_len);
          strncpy(new_icon, state->thermostat->options[EntityPage::_selected_thermostat_option_index]->options[current_index]->icon, new_icon_len);
          new_value[new_value_len] = '\0';
          new_icon[new_icon_len] = '\0';

          taskENTER_CRITICAL(&EntityPage::_entity_page_spinlock);
          if (new_value_len != 0) {
            free(state->thermostat->options[EntityPage::_selected_thermostat_option_index]->current_value);
            state->thermostat->options[EntityPage::_selected_thermostat_option_index]->current_value = new_value;
          }

          if (new_icon_len != 0) {
            free(state->thermostat->options[EntityPage::_selected_thermostat_option_index]->current_icon);
            state->thermostat->options[EntityPage::_selected_thermostat_option_index]->current_icon = new_icon;
          }
          taskEXIT_CRITICAL(&EntityPage::_entity_page_spinlock);
        }

        Nextion::set_component_text(GUI_THERMOSTAT_CONTROL_PAGE::set_label_name, state->thermostat->options[EntityPage::_selected_thermostat_option_index]->current_value, 1000);
        Nextion::set_component_text(GUI_THERMOSTAT_CONTROL_PAGE::options[EntityPage::_selected_thermostat_option_index].icon_name, state->thermostat->options[EntityPage::_selected_thermostat_option_index]->current_icon, 1000);
      }
    } else {
      state->thermostat->set_temperature -= state->thermostat->step_size;
      EntityPage::_update_display_thermostat();
    }
  } else {
    ESP_LOGW("EntityPage", "Unknown string event on thermostat page. Received string: %s", data);
  }
}

void EntityPage::_delete_nspanel_entity_state_object(NSPanelEntityState *object) {
  nspanel_entity_state__free_unpacked(object, NULL);
}

std::shared_ptr<NSPanelEntityState> EntityPage::_get_current_state() {
  if (xSemaphoreTake(EntityPage::_current_state_mutex, pdMS_TO_TICKS(5000)) == pdPASS) [[likely]] {
    std::shared_ptr<NSPanelEntityState> ret = EntityPage::_current_state;
    xSemaphoreGive(EntityPage::_current_state_mutex);
    return ret;
  }
  return nullptr;
}

void EntityPage::_send_thermostat_option_command() {
  if (EntityPage::_is_currently_editing) {
    NSPanelMQTTManagerCommand__ThermostatCommand command = NSPANEL_MQTTMANAGER_COMMAND__THERMOSTAT_COMMAND__INIT;
    std::shared_ptr<NSPanelEntityState> state = EntityPage::_get_current_state();
    command.thermostat_id = state->thermostat->thermostat_id;
    command.option = state->thermostat->options[EntityPage::_selected_thermostat_option_index]->name;
    command.new_value = state->thermostat->options[EntityPage::_selected_thermostat_option_index]->current_value;

    NSPanelMQTTManagerCommand cmd = NSPANEL_MQTTMANAGER_COMMAND__INIT;
    cmd.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_THERMOSTAT_COMMAND;
    cmd.thermostat_command = &command;
    cmd.nspanel_id = NSPM_ConfigManager::get_nspanel_id();

    uint32_t packed_length = nspanel_mqttmanager_command__get_packed_size(&cmd);
    std::vector<uint8_t> buffer(packed_length); // Use vector for automatic cleanup of data when going out of scope
    size_t packed_data_size = nspanel_mqttmanager_command__pack(&cmd, buffer.data());
    if (packed_data_size == packed_length) [[likely]] {
      if (MqttManager::publish(NSPM_ConfigManager::get_manager_command_topic(), (const char *)buffer.data(), packed_length, false) != ESP_OK) [[unlikely]] {
        ESP_LOGE("EntityPage", "Failed to send MQTT message with command payload.");
      }
    } else {
      ESP_LOGE("EntityPage", "Failed to pack protobuf command.");
      EntityPage::_update_display_thermostat(); // Update display to reset values to those stored
    }
  }
}

void EntityPage::_send_thermostat_setpoint_command() {
  if (!EntityPage::_is_currently_editing) {
    NSPanelMQTTManagerCommand__ThermostatTemperatureCommand command = NSPANEL_MQTTMANAGER_COMMAND__THERMOSTAT_TEMPERATURE_COMMAND__INIT;
    std::shared_ptr<NSPanelEntityState> state = EntityPage::_get_current_state();
    command.thermostat_id = state->thermostat->thermostat_id;
    command.temperature = state->thermostat->set_temperature;

    NSPanelMQTTManagerCommand cmd = NSPANEL_MQTTMANAGER_COMMAND__INIT;
    cmd.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_THERMOSTAT_TEMPERATURE_COMMAND;
    cmd.thermostat_temperature_command = &command;
    cmd.nspanel_id = NSPM_ConfigManager::get_nspanel_id();

    uint32_t packed_length = nspanel_mqttmanager_command__get_packed_size(&cmd);
    std::vector<uint8_t> buffer(packed_length); // Use vector for automatic cleanup of data when going out of scope
    size_t packed_data_size = nspanel_mqttmanager_command__pack(&cmd, buffer.data());
    if (packed_data_size == packed_length) [[likely]] {
      if (MqttManager::publish(NSPM_ConfigManager::get_manager_command_topic(), (const char *)buffer.data(), packed_length, false) != ESP_OK) [[unlikely]] {
        ESP_LOGE("EntityPage", "Failed to send MQTT message with command payload.");
      }
    } else {
      ESP_LOGE("EntityPage", "Failed to pack protobuf command.");
      EntityPage::_update_display_thermostat(); // Update display to reset values to those stored
    }

    EntityPage::_update_display_thermostat();
  }
}

const char *EntityPage::_playback_state_name(NSPanelEntityState__MediaPlayer__PlaybackState state) {
  switch (state) {
  case NSPANEL_ENTITY_STATE__MEDIA_PLAYER__PLAYBACK_STATE__OFF:
    return "Off";
  case NSPANEL_ENTITY_STATE__MEDIA_PLAYER__PLAYBACK_STATE__ON:
    return "On";
  case NSPANEL_ENTITY_STATE__MEDIA_PLAYER__PLAYBACK_STATE__IDLE:
    return "Idle";
  case NSPANEL_ENTITY_STATE__MEDIA_PLAYER__PLAYBACK_STATE__PLAYING:
    return "Playing";
  case NSPANEL_ENTITY_STATE__MEDIA_PLAYER__PLAYBACK_STATE__PAUSED:
    return "Paused";
  case NSPANEL_ENTITY_STATE__MEDIA_PLAYER__PLAYBACK_STATE__BUFFERING:
    return "Buffering";
  default:
    return "";
  }
}

void EntityPage::_log_media_player_state(NSPanelEntityState__MediaPlayer *media_player) {
  if (media_player == NULL) [[unlikely]] {
    ESP_LOGE("EntityPage", "Media player state case set but no media player payload.");
    return;
  }

  // The whole decode path runs before anything touches the display, so this verifies the protobuf
  // round trip even on a panel whose TFT has no media player page.
  ESP_LOGD("EntityPage", "Media player %ld '%s': %s", media_player->media_player_id, media_player->name != NULL ? media_player->name : "(no name)", EntityPage::_playback_state_name(media_player->state));
  ESP_LOGD("EntityPage", "  title '%s' artist '%s'", media_player->media_title != NULL ? media_player->media_title : "", media_player->media_artist != NULL ? media_player->media_artist : "");
  ESP_LOGD("EntityPage", "  volume %ld, muted %s, source volume %ld (present: %s)", media_player->volume, media_player->is_muted ? "yes" : "no", media_player->source_volume, media_player->has_source_volume ? "yes" : "no");
  ESP_LOGD("EntityPage", "  can: play %d pause %d next %d prev %d set_volume %d mute %d", media_player->can_play, media_player->can_pause, media_player->can_next_track, media_player->can_previous_track, media_player->can_set_volume, media_player->can_mute);
  ESP_LOGD("EntityPage", "  album art: %s", media_player->album_art_url != NULL && media_player->album_art_url[0] != '\0' ? media_player->album_art_url : "(none)");
}

void EntityPage::_update_display_media_player() {
  ESP_LOGI("EntityPage", "Updating EntityPage with media player state.");
  bool page_just_opened = false;
  if (!EntityPage::_currently_showing) {
    ESP_LOGD("EntityPage", "Switching page to %s", GUI_MEDIA_PLAYER_CONTROL_PAGE::page_name);
    EntityPage::_currently_showing = true;
    if (Nextion::go_to_page(GUI_MEDIA_PLAYER_CONTROL_PAGE::page_name, 1000) != ESP_OK) [[unlikely]] {
      // The official HMI has no media player page yet, so on a stock TFT this fails every time
      // rather than never. Clear the flag before backing out: otherwise the next state update
      // takes the "already showing" path, skips the page switch, and writes the media player
      // components onto whatever page is actually displayed.
      ESP_LOGE("EntityPage", "Failed to navigate Nextion to page. Will go back.");
      EntityPage::_currently_showing = false;
      EntitiesPage::show(EntitiesPage::display_type_t::ENTITIES);
      return;
    }

    InterfaceManager::call_unshow_callback();
    InterfaceManager::current_page_unshow_callback.set(EntityPage::unshow);

    esp_event_handler_register(NEXTION_EVENT, ESP_EVENT_ANY_ID, &EntityPage::_handle_nextion_event, NULL);
    page_just_opened = true;
  }

  std::shared_ptr<NSPanelEntityState> state = EntityPage::_get_current_state();
  NSPanelEntityState__MediaPlayer *media_player = state->media_player;

  if (page_just_opened) {
    EntityPage::_media_player_resolve_component_ids();

    // The protobuf carries no playback position or duration yet, so there is nothing to show.
    Nextion::set_component_visibility(GUI_MEDIA_PLAYER_CONTROL_PAGE::position_bar_name, false, 250);
    if (!EntityPage::_media_player_returning.exchange(false)) {
      EntityPage::_media_player_controlling_source = false;
    }
    taskENTER_CRITICAL(&EntityPage::_entity_page_spinlock);
    EntityPage::_media_player_pending_volume.volume = -1;
    taskEXIT_CRITICAL(&EntityPage::_entity_page_spinlock);
  }

  // Draw the art when the page opens and whenever it changes, for example on the next track. A new
  // render replaces one still in progress, and unshow() cancels one that outlives the page.
  {
    std::string album_art_url = media_player->album_art_url != NULL ? media_player->album_art_url : "";
    std::lock_guard<std::mutex> lock_guard(EntityPage::_media_player_album_art_url_mutex);
    if (page_just_opened || album_art_url != EntityPage::_media_player_album_art_url) {
      EntityPage::_media_player_album_art_url = album_art_url;
      if (!album_art_url.empty()) {
        AlbumArt::render(album_art_url, GUI_MEDIA_PLAYER_CONTROL_PAGE::album_art_x, GUI_MEDIA_PLAYER_CONTROL_PAGE::album_art_y);
      } else {
        AlbumArt::cancel();
      }
    }
  }

  bool is_playing = media_player->state == NSPANEL_ENTITY_STATE__MEDIA_PLAYER__PLAYBACK_STATE__PLAYING ||
                    media_player->state == NSPANEL_ENTITY_STATE__MEDIA_PLAYER__PLAYBACK_STATE__BUFFERING;
  // Fall back to the player if the source has gone away while it was selected.
  bool controlling_source = EntityPage::_media_player_controlling_source && media_player->has_source_volume;

  Nextion::set_component_text(GUI_MEDIA_PLAYER_CONTROL_PAGE::title_label_name, media_player->media_title, 1000);
  Nextion::set_component_text(GUI_MEDIA_PLAYER_CONTROL_PAGE::artist_label_name, media_player->media_artist, 1000);

  // Icons follow the player's state rather than the press, so the page stays right when something
  // else controls the player.
  Nextion::set_component_text(GUI_MEDIA_PLAYER_CONTROL_PAGE::play_pause_button_name, is_playing ? GUI_MEDIA_ICONS::pause : GUI_MEDIA_ICONS::play, 250);
  Nextion::set_component_visibility(GUI_MEDIA_PLAYER_CONTROL_PAGE::play_pause_button_name, is_playing ? media_player->can_pause : media_player->can_play, 250);
  Nextion::set_component_visibility(GUI_MEDIA_PLAYER_CONTROL_PAGE::previous_track_button_name, media_player->can_previous_track, 250);
  Nextion::set_component_visibility(GUI_MEDIA_PLAYER_CONTROL_PAGE::next_track_button_name, media_player->can_next_track, 250);

  // The switch shows what the volume controls act on now: this room's player, or the source feeding it.
  Nextion::set_component_text(GUI_MEDIA_PLAYER_CONTROL_PAGE::source_switch_button_name, controlling_source ? GUI_MEDIA_ICONS::speaker_multiple : GUI_MEDIA_ICONS::speaker, 250);
  Nextion::set_component_visibility(GUI_MEDIA_PLAYER_CONTROL_PAGE::source_switch_button_name, media_player->has_source_volume, 250);

  // The command can mute only the player, so there is no mute while controlling the source.
  Nextion::set_component_text(GUI_MEDIA_PLAYER_CONTROL_PAGE::mute_button_name, media_player->is_muted ? GUI_MEDIA_ICONS::volume_mute : GUI_MEDIA_ICONS::volume_high, 250);
  Nextion::set_component_visibility(GUI_MEDIA_PLAYER_CONTROL_PAGE::mute_button_name, !controlling_source && media_player->can_mute, 250);

  bool can_set_volume = controlling_source || media_player->can_set_volume;
  Nextion::set_component_visibility(GUI_MEDIA_PLAYER_CONTROL_PAGE::volume_down_button_name, can_set_volume, 250);
  Nextion::set_component_visibility(GUI_MEDIA_PLAYER_CONTROL_PAGE::volume_up_button_name, can_set_volume, 250);
  // Only the value: the bar is shown briefly when the volume buttons are pressed, not on every update.
  Nextion::set_component_value(GUI_MEDIA_PLAYER_CONTROL_PAGE::volume_bar_name, EntityPage::_media_player_volume(media_player, controlling_source), 250);
}

void EntityPage::_handle_touch_event_media_player(uint16_t component_id, bool pressed) {
  ESP_LOGD("EntityPage", "Touch component %d, pressed %s", component_id, pressed ? "Yes" : "No");
  if (pressed) {
    return; // The page sends release events only.
  }

  for (const _media_player_component_t &component : EntityPage::_media_player_components) {
    if (component.id != component_id) {
      continue;
    }

    switch (component.action) {
    case _media_player_action::BACK:
      ESP_LOGD("EntityPage", "Received touch event to go back.");
      EntitiesPage::show(EntitiesPage::display_type_t::ENTITIES);
      break;

    case _media_player_action::PLAY_PAUSE:
      EntityPage::_media_player_play_pause();
      break;

    case _media_player_action::PREVIOUS_TRACK:
      EntityPage::_media_player_previous_track();
      break;

    case _media_player_action::NEXT_TRACK:
      EntityPage::_media_player_next_track();
      break;

    case _media_player_action::MUTE:
      EntityPage::_media_player_toggle_mute();
      break;

    case _media_player_action::SOURCE_SWITCH:
      EntityPage::_media_player_toggle_source();
      break;

    case _media_player_action::VOLUME_UP:
      EntityPage::_media_player_step_volume(1);
      break;

    case _media_player_action::VOLUME_DOWN:
      EntityPage::_media_player_step_volume(-1);
      break;

    case _media_player_action::ALBUM_ART: {
      // The source list follows the source switch, like the volume buttons.
      std::shared_ptr<NSPanelEntityState> state = EntityPage::_get_current_state();
      bool source_player = EntityPage::_media_player_controlling_source && state != nullptr && state->entity_case == NSPANEL_ENTITY_STATE__ENTITY_MEDIA_PLAYER && state->media_player->has_source_volume;
      MediaSourcePage::show(EntityPage::_current_entity_mqtt_topic, source_player);
      break;
    }
    }
    return;
  }
}

void EntityPage::_media_player_resolve_component_ids() {
  if (EntityPage::_media_player_component_ids_resolved) {
    return;
  }

  bool all_resolved = true;
  std::string resolved_ids;
  for (_media_player_component_t &component : EntityPage::_media_player_components) {
    int32_t id = -1;
    if (Nextion::get_component_integer_attribute(component.name, "id", &id, 250, 250) == ESP_OK && id >= 0 && id <= UINT8_MAX) {
      component.id = id;
    } else {
      // Keep the ID from GUI_data.hpp and try again the next time the page opens.
      ESP_LOGE("EntityPage", "Failed to look up the ID of media player component %s, will use %d.", component.name, component.id.load());
      all_resolved = false;
    }
    resolved_ids.append(" ").append(component.name).append("=").append(std::to_string(component.id.load()));
  }
  ESP_LOGI("EntityPage", "Media player component IDs:%s", resolved_ids.c_str());
  EntityPage::_media_player_component_ids_resolved = all_resolved;
}

void EntityPage::_media_player_play_pause() {
  std::shared_ptr<NSPanelEntityState> state = EntityPage::_get_current_state();
  if (state == nullptr || state->entity_case != NSPANEL_ENTITY_STATE__ENTITY_MEDIA_PLAYER) [[unlikely]] {
    ESP_LOGE("EntityPage", "Tried to play/pause without a media player state.");
    return;
  }

  NSPanelMQTTManagerCommand__MediaPlayerCommand command = NSPANEL_MQTTMANAGER_COMMAND__MEDIA_PLAYER_COMMAND__INIT;
  if (state->media_player->state == NSPANEL_ENTITY_STATE__MEDIA_PLAYER__PLAYBACK_STATE__PLAYING || state->media_player->state == NSPANEL_ENTITY_STATE__MEDIA_PLAYER__PLAYBACK_STATE__BUFFERING) {
    command.playback_action = NSPANEL_MQTTMANAGER_COMMAND__MEDIA_PLAYER_COMMAND__PLAYBACK_ACTION__PAUSE;
  } else {
    command.playback_action = NSPANEL_MQTTMANAGER_COMMAND__MEDIA_PLAYER_COMMAND__PLAYBACK_ACTION__PLAY;
  }
  EntityPage::_send_media_player_command(&command);
}

void EntityPage::_media_player_next_track() {
  NSPanelMQTTManagerCommand__MediaPlayerCommand command = NSPANEL_MQTTMANAGER_COMMAND__MEDIA_PLAYER_COMMAND__INIT;
  command.playback_action = NSPANEL_MQTTMANAGER_COMMAND__MEDIA_PLAYER_COMMAND__PLAYBACK_ACTION__NEXT_TRACK;
  EntityPage::_send_media_player_command(&command);
}

void EntityPage::_media_player_previous_track() {
  NSPanelMQTTManagerCommand__MediaPlayerCommand command = NSPANEL_MQTTMANAGER_COMMAND__MEDIA_PLAYER_COMMAND__INIT;
  command.playback_action = NSPANEL_MQTTMANAGER_COMMAND__MEDIA_PLAYER_COMMAND__PLAYBACK_ACTION__PREVIOUS_TRACK;
  EntityPage::_send_media_player_command(&command);
}

void EntityPage::_media_player_toggle_mute() {
  std::shared_ptr<NSPanelEntityState> state = EntityPage::_get_current_state();
  if (state == nullptr || state->entity_case != NSPANEL_ENTITY_STATE__ENTITY_MEDIA_PLAYER) [[unlikely]] {
    ESP_LOGE("EntityPage", "Tried to toggle mute without a media player state.");
    return;
  }

  NSPanelMQTTManagerCommand__MediaPlayerCommand command = NSPANEL_MQTTMANAGER_COMMAND__MEDIA_PLAYER_COMMAND__INIT;
  command.has_muted = true;
  command.muted = !state->media_player->is_muted;
  EntityPage::_send_media_player_command(&command);
}

void EntityPage::_media_player_set_volume(int32_t volume) {
  NSPanelMQTTManagerCommand__MediaPlayerCommand command = NSPANEL_MQTTMANAGER_COMMAND__MEDIA_PLAYER_COMMAND__INIT;
  command.has_volume = true;
  command.volume = volume;
  EntityPage::_send_media_player_command(&command);
}

void EntityPage::_media_player_set_source_volume(int32_t volume) {
  NSPanelMQTTManagerCommand__MediaPlayerCommand command = NSPANEL_MQTTMANAGER_COMMAND__MEDIA_PLAYER_COMMAND__INIT;
  command.has_source_volume = true;
  command.source_volume = volume;
  EntityPage::_send_media_player_command(&command);
}

void EntityPage::_media_player_step_volume(int32_t direction) {
  std::shared_ptr<NSPanelEntityState> state = EntityPage::_get_current_state();
  if (state == nullptr || state->entity_case != NSPANEL_ENTITY_STATE__ENTITY_MEDIA_PLAYER) [[unlikely]] {
    ESP_LOGE("EntityPage", "Tried to change volume without a media player state.");
    return;
  }
  NSPanelEntityState__MediaPlayer *media_player = state->media_player;

  bool controlling_source = EntityPage::_media_player_controlling_source && media_player->has_source_volume;
  // A manager from before volume_step was added sends 0.
  int32_t step = media_player->volume_step > 0 ? media_player->volume_step : EntityPage::_media_player_default_volume_step;
  // Step from the pending volume if there is one, so quick presses compound instead of each
  // starting from a state that has not caught up yet.
  int32_t volume = std::clamp<int32_t>(EntityPage::_media_player_volume(media_player, controlling_source) + direction * step, 0, 100);

  taskENTER_CRITICAL(&EntityPage::_entity_page_spinlock);
  EntityPage::_media_player_pending_volume = {
      .volume = volume,
      .source = controlling_source,
      .requested_at = esp_timer_get_time(),
  };
  taskEXIT_CRITICAL(&EntityPage::_entity_page_spinlock);

  if (controlling_source) {
    EntityPage::_media_player_set_source_volume(volume);
  } else {
    EntityPage::_media_player_set_volume(volume);
  }
  EntityPage::_media_player_show_volume_bar(volume);
}

int32_t EntityPage::_media_player_volume(const NSPanelEntityState__MediaPlayer *media_player, bool source) {
  int32_t volume = source ? media_player->source_volume : media_player->volume;

  taskENTER_CRITICAL(&EntityPage::_entity_page_spinlock);
  const _media_player_pending_volume_t pending = EntityPage::_media_player_pending_volume;
  taskEXIT_CRITICAL(&EntityPage::_entity_page_spinlock);

  if (pending.volume >= 0 && pending.source == source && esp_timer_get_time() - pending.requested_at < EntityPage::_media_player_pending_volume_timeout_us) {
    volume = pending.volume;
  }
  return volume;
}

void EntityPage::_media_player_confirm_pending_volume(const NSPanelEntityState__MediaPlayer *media_player) {
  if (media_player == NULL) [[unlikely]] {
    return;
  }

  // Only the state for the last press confirms it. States for earlier presses in a quick run arrive
  // in between, and dropping the pending volume on those would make the bar jump back.
  taskENTER_CRITICAL(&EntityPage::_entity_page_spinlock);
  _media_player_pending_volume_t &pending = EntityPage::_media_player_pending_volume;
  if (pending.volume >= 0 && pending.volume == (pending.source ? media_player->source_volume : media_player->volume)) {
    pending.volume = -1;
  }
  taskEXIT_CRITICAL(&EntityPage::_entity_page_spinlock);
}

void EntityPage::_media_player_toggle_source() {
  std::shared_ptr<NSPanelEntityState> state = EntityPage::_get_current_state();
  if (state == nullptr || state->entity_case != NSPANEL_ENTITY_STATE__ENTITY_MEDIA_PLAYER) [[unlikely]] {
    ESP_LOGE("EntityPage", "Tried to switch to the source without a media player state.");
    return;
  }
  if (!state->media_player->has_source_volume) {
    return; // The switch is hidden, but a press can race the state update that hid it.
  }

  EntityPage::_media_player_controlling_source = !EntityPage::_media_player_controlling_source;
  ESP_LOGD("EntityPage", "Volume controls now act on the %s.", EntityPage::_media_player_controlling_source ? "source" : "player");
  EntityPage::_update_display_media_player();
}

void EntityPage::_media_player_show_volume_bar(int32_t volume) {
  Nextion::set_component_value(GUI_MEDIA_PLAYER_CONTROL_PAGE::volume_bar_name, volume, 250);
  Nextion::set_component_visibility(GUI_MEDIA_PLAYER_CONTROL_PAGE::volume_bar_name, true, 250);

  // The timer hides the bar. Stop it before starting it so that every press restarts the countdown,
  // and holding the buttons keeps the bar up.
  std::string timer = GUI_MEDIA_PLAYER_CONTROL_PAGE::volume_bar_timer_name;
  Nextion::send_raw_command((timer + ".en=0").c_str(), 250);
  Nextion::send_raw_command((timer + ".en=1").c_str(), 250);
}

void EntityPage::_send_media_player_command(NSPanelMQTTManagerCommand__MediaPlayerCommand *command) {
  std::shared_ptr<NSPanelEntityState> state = EntityPage::_get_current_state();
  if (state == nullptr || state->entity_case != NSPANEL_ENTITY_STATE__ENTITY_MEDIA_PLAYER) [[unlikely]] {
    ESP_LOGE("EntityPage", "Tried to send media player command without a media player state.");
    return;
  }
  command->media_player_id = state->media_player->media_player_id;

  NSPanelMQTTManagerCommand cmd = NSPANEL_MQTTMANAGER_COMMAND__INIT;
  cmd.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_MEDIA_PLAYER_COMMAND;
  cmd.media_player_command = command;
  cmd.nspanel_id = NSPM_ConfigManager::get_nspanel_id();

  uint32_t packed_length = nspanel_mqttmanager_command__get_packed_size(&cmd);
  std::vector<uint8_t> buffer(packed_length); // Use vector for automatic cleanup of data when going out of scope
  size_t packed_data_size = nspanel_mqttmanager_command__pack(&cmd, buffer.data());
  if (packed_data_size == packed_length) [[likely]] {
    if (MqttManager::publish(NSPM_ConfigManager::get_manager_command_topic(), (const char *)buffer.data(), packed_length, false) != ESP_OK) [[unlikely]] {
      ESP_LOGE("EntityPage", "Failed to send MQTT message with command payload.");
      EntityPage::_update_display_media_player(); // Update display to reset values to those stored
    }
  } else {
    ESP_LOGE("EntityPage", "Failed to pack protobuf command.");
    EntityPage::_update_display_media_player(); // Update display to reset values to those stored
  }
}
