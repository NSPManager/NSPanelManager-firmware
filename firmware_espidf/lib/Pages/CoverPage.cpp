#include <CoverPage.hpp>
#include <ConfigManager.hpp>
#include <EntitiesPage.hpp>
#include <GUI_data.hpp>
#include <MqttManager.hpp>
#include <NSPM_ConfigManager.hpp>
#include <Nextion.hpp>
#include <cstdio>
#include <esp_log.h>
#include <esp_timer.h>
#include <vector>

CoverPage::_component_t CoverPage::_components[] = {
    {GUI_COVER_CONTROL_PAGE::back_button_name, _action::BACK, GUI_COVER_CONTROL_PAGE::back_button_id},
    {GUI_COVER_CONTROL_PAGE::open_button_name, _action::OPEN, GUI_COVER_CONTROL_PAGE::open_button_id},
    {GUI_COVER_CONTROL_PAGE::stop_button_name, _action::STOP, GUI_COVER_CONTROL_PAGE::stop_button_id},
    {GUI_COVER_CONTROL_PAGE::close_button_name, _action::CLOSE, GUI_COVER_CONTROL_PAGE::close_button_id},
    {GUI_COVER_CONTROL_PAGE::position_slider_name, _action::POSITION, GUI_COVER_CONTROL_PAGE::position_slider_id},
};

void CoverPage::unshow() {
  CoverPage::_showing = false;
  CoverPage::_pending_position = -1;
}

void CoverPage::_set_visible(const char *component_name, bool visible) {
  Nextion::set_component_visibility(component_name, visible, 250);
}

const char *CoverPage::_state_text(NSPanelEntityState__Cover__CoverState state) {
  switch (state) {
  case NSPANEL_ENTITY_STATE__COVER__COVER_STATE__OPEN:
    return "Open";
  case NSPANEL_ENTITY_STATE__COVER__COVER_STATE__CLOSED:
    return "Closed";
  case NSPANEL_ENTITY_STATE__COVER__COVER_STATE__OPENING:
    return "Opening...";
  case NSPANEL_ENTITY_STATE__COVER__COVER_STATE__CLOSING:
    return "Closing...";
  case NSPANEL_ENTITY_STATE__COVER__COVER_STATE__UNAVAILABLE:
    return "Unavailable";
  case NSPANEL_ENTITY_STATE__COVER__COVER_STATE__UNKNOWN:
  default:
    return "Unknown";
  }
}

void CoverPage::update_display(const NSPanelEntityState__Cover *cover, bool page_just_opened) {
  if (cover == NULL) [[unlikely]] {
    return;
  }

  CoverPage::_showing = true;
  CoverPage::_cover_id = cover->cover_id;
  if (page_just_opened) {
    if (!CoverPage::_resolve_component_ids()) {
      // A stock TFT has no cover page. Do not write cover component names onto whatever page is
      // actually displayed; go back to the entities page instead.
      ESP_LOGE("CoverPage", "The cover page is missing or its components are not named as expected. Will go back.");
      CoverPage::_showing = false;
      EntitiesPage::show(EntitiesPage::display_type_t::ENTITIES);
      return;
    }
    CoverPage::_pending_position = -1;
  }

  Nextion::set_component_text(GUI_COVER_CONTROL_PAGE::name_label_name, cover->name, 500);
  if (page_just_opened) {
    // Button labels are set at runtime rather than stored in the HMI, and they are glyphs rather
    // than words so a single up/stop/down set reads the same for blinds, shutters and awnings.
    Nextion::set_component_text(GUI_COVER_CONTROL_PAGE::open_button_name, GUI_COVER_CONTROL_PAGE::open_button_text, 250);
    Nextion::set_component_text(GUI_COVER_CONTROL_PAGE::stop_button_name, GUI_COVER_CONTROL_PAGE::stop_button_text, 250);
    Nextion::set_component_text(GUI_COVER_CONTROL_PAGE::close_button_name, GUI_COVER_CONTROL_PAGE::close_button_text, 250);
  }

  // Drop a pending position once the cover reports it, or once the request has had time to be
  // carried out. The panel then follows the controller again.
  int32_t pending = CoverPage::_pending_position;
  if (pending >= 0) {
    int64_t age = esp_timer_get_time() - CoverPage::_pending_position_requested_at;
    if ((cover->has_position && cover->position == pending) || age > CoverPage::_pending_position_timeout_us) {
      CoverPage::_pending_position = -1;
      pending = -1;
    }
  }

  // The page has one state label instead of separate state and position labels, so combine them.
  const char *state_text = CoverPage::_state_text(cover->state);
  char combined_text[40];
  if (cover->can_set_position) {
    CoverPage::_set_visible(GUI_COVER_CONTROL_PAGE::position_slider_name, true);
    int32_t shown_position = pending >= 0 ? pending : (cover->has_position ? cover->position : 0);
    Nextion::set_component_value(GUI_COVER_CONTROL_PAGE::position_slider_name, shown_position, 250);
    if (pending >= 0 || cover->has_position) {
      snprintf(combined_text, sizeof(combined_text), "%s %d%%", state_text, static_cast<int>(shown_position));
    } else {
      snprintf(combined_text, sizeof(combined_text), "%s", state_text);
    }
  } else {
    CoverPage::_set_visible(GUI_COVER_CONTROL_PAGE::position_slider_name, false);
    snprintf(combined_text, sizeof(combined_text), "%s", state_text);
  }
  Nextion::set_component_text(GUI_COVER_CONTROL_PAGE::state_label_name, combined_text, 250);

  CoverPage::_set_visible(GUI_COVER_CONTROL_PAGE::open_button_name, cover->can_open);
  CoverPage::_set_visible(GUI_COVER_CONTROL_PAGE::stop_button_name, cover->can_stop);
  CoverPage::_set_visible(GUI_COVER_CONTROL_PAGE::close_button_name, cover->can_close);
}

bool CoverPage::_resolve_component_ids() {
  if (CoverPage::_component_ids_resolved) {
    return true;
  }

  bool all_resolved = true;
  for (CoverPage::_component_t &component : CoverPage::_components) {
    int32_t id = -1;
    if (Nextion::get_component_integer_attribute(component.name, "id", &id, 250, 250) == ESP_OK && id >= 0 && id <= UINT8_MAX) {
      component.id = id;
    } else {
      // Keep the fallback ID from GUI_data.hpp and try again next time the page opens.
      ESP_LOGE("CoverPage", "Failed to look up the ID of cover component %s, will use %d.", component.name, component.id.load());
      all_resolved = false;
    }
  }
  CoverPage::_component_ids_resolved = all_resolved;
  return all_resolved;
}

void CoverPage::_send_command(NSPanelMQTTManagerCommand__CoverCommand *cover_command) {
  NSPanelMQTTManagerCommand command = NSPANEL_MQTTMANAGER_COMMAND__INIT;
  command.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_COVER_COMMAND;
  command.cover_command = cover_command;
  command.nspanel_id = NSPM_ConfigManager::get_nspanel_id();

  uint32_t packed_length = nspanel_mqttmanager_command__get_packed_size(&command);
  std::vector<uint8_t> buffer(packed_length);
  size_t packed_data_size = nspanel_mqttmanager_command__pack(&command, buffer.data());
  if (packed_data_size != packed_length) [[unlikely]] {
    ESP_LOGE("CoverPage", "Failed to pack cover command.");
    return;
  }

  if (MqttManager::publish(NSPM_ConfigManager::get_manager_command_topic(), (const char *)buffer.data(), packed_length, false) != ESP_OK) {
    ESP_LOGE("CoverPage", "Failed to send cover command.");
  }
}

void CoverPage::_send_action(NSPanelMQTTManagerCommand__CoverCommand__Action action) {
  NSPanelMQTTManagerCommand__CoverCommand command = NSPANEL_MQTTMANAGER_COMMAND__COVER_COMMAND__INIT;
  command.cover_id = CoverPage::_cover_id;
  command.action = action;
  CoverPage::_send_command(&command);
}

void CoverPage::_send_position(uint8_t position) {
  NSPanelMQTTManagerCommand__CoverCommand command = NSPANEL_MQTTMANAGER_COMMAND__COVER_COMMAND__INIT;
  command.cover_id = CoverPage::_cover_id;
  command.has_position = true;
  command.position = position;
  CoverPage::_pending_position = position;
  CoverPage::_pending_position_requested_at = esp_timer_get_time();
  CoverPage::_send_command(&command);
}

void CoverPage::handle_touch(uint16_t component_id, bool pressed) {
  if (!CoverPage::_showing) {
    return;
  }

  for (CoverPage::_component_t &component : CoverPage::_components) {
    if (component.id != component_id) {
      continue;
    }

    switch (component.action) {
    case _action::BACK:
      if (pressed) {
        EntitiesPage::show(EntitiesPage::display_type_t::ENTITIES);
      }
      return;

    case _action::OPEN:
      if (pressed) {
        CoverPage::_send_action(NSPANEL_MQTTMANAGER_COMMAND__COVER_COMMAND__ACTION__OPEN);
      }
      return;

    case _action::STOP:
      if (pressed) {
        CoverPage::_send_action(NSPANEL_MQTTMANAGER_COMMAND__COVER_COMMAND__ACTION__STOP);
      }
      return;

    case _action::CLOSE:
      if (pressed) {
        CoverPage::_send_action(NSPANEL_MQTTMANAGER_COMMAND__COVER_COMMAND__ACTION__CLOSE);
      }
      return;

    case _action::POSITION: {
      // Match the light page's slider handling: the Nextion only sends this event for the
      // component with "sendkey" enabled, and its value is the current slider position.
      int32_t position = 0;
      if (Nextion::get_component_integer_value(GUI_COVER_CONTROL_PAGE::position_slider_name, &position, 250, 250) == ESP_OK && position >= 0 && position <= 100) {
        CoverPage::_send_position(static_cast<uint8_t>(position));
      } else {
        ESP_LOGE("CoverPage", "Failed to read the cover position slider.");
      }
      return;
    }
    }
  }
}
