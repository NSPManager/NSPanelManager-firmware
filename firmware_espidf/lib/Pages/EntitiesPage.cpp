#include <EntitiesPage.hpp>
#include <GUI_data.hpp>
#include <HomePage.hpp>
#include <InterfaceManager.hpp>
#include <Nextion.hpp>
#include <Nextion_event.hpp>
#include <RoomManager.hpp>
#include <RoomManager_event.hpp>
#include <esp_log.h>
#include <format>

void EntitiesPage::show() {
  esp_log_level_set("EntitiesPage", esp_log_level_t::ESP_LOG_DEBUG); // TODO: Load from config

  InterfaceManager::call_unshow_callback();
  InterfaceManager::current_page_unshow_callback.set(EntitiesPage::unshow);

  if (RoomManager::get_current_room_entities_page_status(&EntitiesPage::_current_entities_page) != ESP_OK) {
    ESP_LOGE("EntitiesPage", "Failed to get current room entities. Will return to HomePage.");
    HomePage::show();
    return;
  }

  EntitiesPage::_update_display();
  RoomManager::register_handler(ESP_EVENT_ANY_ID, EntitiesPage::_handle_roommanager_event, NULL);
  esp_event_handler_register(NEXTION_EVENT, ESP_EVENT_ANY_ID, EntitiesPage::_handle_nextion_event, NULL);
}

void EntitiesPage::unshow() {
  RoomManager::unregister_handler(ESP_EVENT_ANY_ID, EntitiesPage::_handle_roommanager_event);
  esp_event_handler_unregister(NEXTION_EVENT, ESP_EVENT_ANY_ID, EntitiesPage::_handle_nextion_event);

  EntitiesPage::_currently_showing_page_type = 0;       // Set to 0 to force a page navigation on Nextion display when showing EntitiesPage next time.
  EntitiesPage::_currently_showing_header_text.set(""); // Clear stored string so that header text is always updated when showing EntitiesPage.
}

void EntitiesPage::_update_display() {
  if (EntitiesPage::_current_entities_page == nullptr) {
    ESP_LOGE("EntitiesPage", "Tried to show RoomEntitiesPage but no _current_room_status has been set. Returning to HomePage.");
    HomePage::show();
    return;
  }

  switch (EntitiesPage::_current_entities_page->page_type) {
  case 4: {
    if (EntitiesPage::_currently_showing_page_type != 4) {
      Nextion::go_to_page(GUI_ITEMS4_PAGE::page_name, 1000);
      EntitiesPage::_currently_showing_page_type = 4;
    }
    break;
  }

  case 8: {
    if (EntitiesPage::_currently_showing_page_type != 8) {
      Nextion::go_to_page(GUI_ITEMS8_PAGE::page_name, 1000);
      EntitiesPage::_currently_showing_page_type = 8;
    }
    break;
  }

  case 12: {
    if (EntitiesPage::_currently_showing_page_type != 12) {
      Nextion::go_to_page(GUI_ITEMS12_PAGE::page_name, 1000);
      EntitiesPage::_currently_showing_page_type = 12;
    }
    break;
  }

  default:
    ESP_LOGE("EntitiesPage", "Unknown entities page type! Type: %ld. Will navigate to home page.", EntitiesPage::_current_entities_page->page_type);
    HomePage::show();
    return;
  }

  EntitiesPage::_update_displayed_items();

  if (std::string(EntitiesPage::_current_entities_page->header_text).compare(EntitiesPage::_currently_showing_header_text.get()) != 0) {
    Nextion::set_component_text(GUI_ITEMS_PAGE_COMMON::page_header_label, EntitiesPage::_current_entities_page->header_text, 250);
    EntitiesPage::_currently_showing_header_text.set(EntitiesPage::_current_entities_page->header_text);
  }
}

void EntitiesPage::_update_displayed_items() {
  for (int i = 0; i < EntitiesPage::_current_entities_page->page_type; i++) {
    NSPanelRoomEntitiesPage__EntitySlot *slot = nullptr;
    for (int j = 0; j < EntitiesPage::_current_entities_page->n_entities; j++) {
      if (EntitiesPage::_current_entities_page->entities[j]->room_view_position == i) {
        slot = EntitiesPage::_current_entities_page->entities[j];
        break;
      }
    }

    if (EntitiesPage::_current_entities_page->page_type == 12) {
      EntitiesPage::_update_displayed_item_in_slot(slot, GUI_ITEMS12_PAGE::item_slots[i]);
    } else if (EntitiesPage::_current_entities_page->page_type == 8) {
      EntitiesPage::_update_displayed_item_in_slot(slot, GUI_ITEMS8_PAGE::item_slots[i]);
    } else if (EntitiesPage::_current_entities_page->page_type == 4) {
      EntitiesPage::_update_displayed_item_in_slot(slot, GUI_ITEMS4_PAGE::item_slots[i]);
    } else {
      ESP_LOGE("EntitiesPage", "Unknown entities page type: %ld. Will not update displayed entity slot.", EntitiesPage::_current_entities_page->page_type);
    }
  }
}

void EntitiesPage::_update_displayed_item_in_slot(NSPanelRoomEntitiesPage__EntitySlot *slot_data, const GUI_ITEMS_PAGE_ITEM_DATA page_slot) {
  if (slot_data != nullptr) {
    Nextion::set_component_text(page_slot.label_name, std::format("   {}", slot_data->name).c_str(), 250);
    Nextion::set_component_text(page_slot.button_name, slot_data->icon, 250);
    Nextion::set_component_foreground(page_slot.button_name, slot_data->pco, 250);
    Nextion::set_component_pco2(page_slot.button_name, slot_data->pco2, 250);

    Nextion::set_component_visibility(page_slot.button_name, true, 250);
    Nextion::set_component_visibility(page_slot.label_name, true, 250);
  } else {
    // No entity was found for page slot. Blank page slot.
    Nextion::set_component_visibility(page_slot.button_name, false, 250);
    Nextion::set_component_visibility(page_slot.label_name, false, 250);
  }
}

void EntitiesPage::_handle_roommanager_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id == roommanager_event_t::ROOM_ENTITIES_PAGE_UPDATED) {
    ESP_LOGD("EntitiesPage", "Received new data. Will update display.");
    if (RoomManager::get_current_room_entities_page_status(&EntitiesPage::_current_entities_page) == ESP_OK) [[likely]] {
      EntitiesPage::_update_display();
    } else {
      ESP_LOGE("EntitiesPage", "Got new entity page data event but couldn't got new entity page data!");
    }
  }
}

void EntitiesPage::_handle_nextion_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  switch (event_id) {
  case nextion_event_t::TOUCH_EVENT: {
    nextion_event_touch_t *data = (nextion_event_touch_t *)event_data;
    switch (EntitiesPage::_current_entities_page->page_type) {
    case 4:
      EntitiesPage::_handle_items4_touch_event(data);
      break;
    case 8:
      EntitiesPage::_handle_items8_touch_event(data);
      break;
    case 12:
      EntitiesPage::_handle_items12_touch_event(data);
      break;

    default:
      ESP_LOGE("EntitiesPage", "Unknown entities page type! Type: %ld while handling page touch event!", EntitiesPage::_current_entities_page->page_type);
      HomePage::show();
      return;
    }

    break;
  }

  default:
    break;
  }
}

void EntitiesPage::_handle_items4_touch_event(nextion_event_touch_t *touch_data) {
  switch (touch_data->component_id) {
  case GUI_ITEMS4_PAGE::button_back_id:
    HomePage::show();
    break;

  case GUI_ITEMS4_PAGE::button_previous_page_id:
    RoomManager::go_to_previous_entities_page();
    break;

  case GUI_ITEMS4_PAGE::button_next_page_id:
    RoomManager::go_to_next_entities_page();
    break;

  default:
    ESP_LOGD("EntitiesPage", "Unknown component ID of touch event in items4. ID: %u", touch_data->component_id);
    break;
  }
}

void EntitiesPage::_handle_items8_touch_event(nextion_event_touch_t *touch_data) {
  switch (touch_data->component_id) {
  case GUI_ITEMS8_PAGE::button_back_id:
    HomePage::show();
    break;

  case GUI_ITEMS8_PAGE::button_previous_page_id:
    RoomManager::go_to_previous_entities_page();
    break;

  case GUI_ITEMS8_PAGE::button_next_page_id:
    RoomManager::go_to_next_entities_page();
    break;

  default:
    ESP_LOGD("EntitiesPage", "Unknown component ID of touch event in items8. ID: %u", touch_data->component_id);
    break;
  }
}

void EntitiesPage::_handle_items12_touch_event(nextion_event_touch_t *touch_data) {
  switch (touch_data->component_id) {
  case GUI_ITEMS12_PAGE::button_back_id:
    HomePage::show();
    break;

  case GUI_ITEMS12_PAGE::button_previous_page_id:
    RoomManager::go_to_previous_entities_page();
    break;

  case GUI_ITEMS12_PAGE::button_next_page_id:
    RoomManager::go_to_next_entities_page();
    break;
  default:
    ESP_LOGD("EntitiesPage", "Unknown component ID of touch event in items12. ID: %u", touch_data->component_id);
    break;
  }
}