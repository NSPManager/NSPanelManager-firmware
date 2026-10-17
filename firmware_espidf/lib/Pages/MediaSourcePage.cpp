#include <ConfigManager.hpp>
#include <EntityPage.hpp>
#include <GUI_data.hpp>
#include <InterfaceManager.hpp>
#include <MediaSourcePage.hpp>
#include <Nextion.hpp>
#include <Nextion_event.hpp>
#include <esp_log.h>

// Until the media player state carries its sources (NSPanelManager PR #396), show those of an audio
// matrix zone and of the Sonos speaker feeding it. Each list keeps the source last selected from it.
static const std::vector<std::string> hard_coded_player_sources = {"Movie Theatre", "Gym TV", "James Matrix", "Jo Matrix", "Disconnected", "Kitchen TV", "Alexa 1"};
static std::string hard_coded_player_current_source = "James Matrix";
static const std::vector<std::string> hard_coded_source_player_sources = {
    "Line-in",
    "Bringing It All Back Home",
    "Brothers In Arms",
    "Christmas",
    "Continuum",
    "Eye In the Sky",
    "James Mulcahy's Station",
    "Mmm Mmm Mmm Mmm",
    "Peaches!",
    "Pomplamoose & Similar Artists Station",
    "Scary Goldings: Live (feat. John Scofield)",
    "True (Bonus Edition)",
    "Vertigo",
    "Workout playlist",
    "Year of the Cat",
    "ST Music",
};
static std::string hard_coded_source_player_current_source = ""; // Sonos reports no source for a playing favorite.

void MediaSourcePage::show(std::string media_player_state_topic, bool source_player) {
  esp_log_level_set("MediaSourcePage", ConfigManager::log_level);

  InterfaceManager::call_unshow_callback();
  InterfaceManager::current_page_unshow_callback.set(MediaSourcePage::unshow);

  MediaSourcePage::_media_player_state_topic = media_player_state_topic;
  MediaSourcePage::_showing_source_player = source_player;
  MediaSourcePage::_sources = source_player ? hard_coded_source_player_sources : hard_coded_player_sources;
  MediaSourcePage::_current_source = source_player ? hard_coded_source_player_current_source : hard_coded_player_current_source;
  MediaSourcePage::_first_source_index = 0;

  if (MediaSourcePage::_sources.size() <= 4) {
    MediaSourcePage::_page_type = 4;
  } else if (MediaSourcePage::_sources.size() <= 8) {
    MediaSourcePage::_page_type = 8;
  } else {
    MediaSourcePage::_page_type = 12;
  }

  esp_event_handler_register(NEXTION_EVENT, ESP_EVENT_ANY_ID, &MediaSourcePage::_handle_nextion_event, NULL);
  MediaSourcePage::_update_display();
}

void MediaSourcePage::unshow() {
  esp_event_handler_unregister(NEXTION_EVENT, ESP_EVENT_ANY_ID, &MediaSourcePage::_handle_nextion_event);
  MediaSourcePage::_page_type = 0;
}

void MediaSourcePage::_update_display() {
  switch (MediaSourcePage::_page_type) {
  case 4:
    Nextion::go_to_page(GUI_ITEMS4_PAGE::page_name, 1000);
    break;
  case 8:
    Nextion::go_to_page(GUI_ITEMS8_PAGE::page_name, 1000);
    break;
  case 12:
    Nextion::go_to_page(GUI_ITEMS12_PAGE::page_name, 1000);
    break;
  default:
    return;
  }

  Nextion::set_component_text(GUI_ITEMS_PAGE_COMMON::page_header_label, MediaSourcePage::_showing_source_player ? "Favorites" : "Input", 250);
  for (size_t slot = 0; slot < MediaSourcePage::_page_type; slot++) {
    size_t source_index = MediaSourcePage::_first_source_index + slot;
    if (MediaSourcePage::_page_type == 12) {
      MediaSourcePage::_update_displayed_source_in_slot(source_index, GUI_ITEMS12_PAGE::item_slots[slot]);
    } else if (MediaSourcePage::_page_type == 8) {
      MediaSourcePage::_update_displayed_source_in_slot(source_index, GUI_ITEMS8_PAGE::item_slots[slot]);
    } else {
      MediaSourcePage::_update_displayed_source_in_slot(source_index, GUI_ITEMS4_PAGE::item_slots[slot]);
    }
  }
}

void MediaSourcePage::_update_displayed_source_in_slot(size_t source_index, const GUI_ITEMS_PAGE_ITEM_DATA page_slot) {
  if (source_index < MediaSourcePage::_sources.size()) {
    const std::string &source = MediaSourcePage::_sources[source_index];
    bool is_current = source == MediaSourcePage::_current_source;
    Nextion::set_component_text(page_slot.label_name, (std::string("   ") + source).c_str(), 250);
    Nextion::set_component_text(page_slot.button_name, GUI_ICONS::speaker, 250);
    Nextion::set_component_foreground(page_slot.button_name, is_current ? GUI_ITEMS_PAGE_COMMON::items_button_on_pco : GUI_ITEMS_PAGE_COMMON::items_button_off_pco, 250);
    Nextion::set_component_pco2(page_slot.button_name, is_current ? GUI_ITEMS_PAGE_COMMON::items_button_on_pco2 : GUI_ITEMS_PAGE_COMMON::items_button_off_pco2, 250);
    Nextion::set_component_visibility(page_slot.button_name, true, 250);
    Nextion::set_component_visibility(page_slot.label_name, true, 250);
    vTaskDelay(pdMS_TO_TICKS(50)); // As EntitiesPage does between slots, so the display keeps up.
  } else {
    Nextion::set_component_visibility(page_slot.button_name, false, 250);
    Nextion::set_component_visibility(page_slot.label_name, false, 250);
  }
}

void MediaSourcePage::_handle_nextion_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id != nextion_event_t::TOUCH_EVENT) {
    return;
  }

  nextion_event_touch_t *touch_data = (nextion_event_touch_t *)event_data;
  switch (MediaSourcePage::_page_type) {
  case 4:
    MediaSourcePage::_handle_touch_event(touch_data, GUI_ITEMS4_PAGE::button_back_id, GUI_ITEMS4_PAGE::button_previous_page_id, GUI_ITEMS4_PAGE::button_next_page_id, GUI_ITEMS4_PAGE::item_slots);
    break;
  case 8:
    MediaSourcePage::_handle_touch_event(touch_data, GUI_ITEMS8_PAGE::button_back_id, GUI_ITEMS8_PAGE::button_previous_page_id, GUI_ITEMS8_PAGE::button_next_page_id, GUI_ITEMS8_PAGE::item_slots);
    break;
  case 12:
    MediaSourcePage::_handle_touch_event(touch_data, GUI_ITEMS12_PAGE::button_back_id, GUI_ITEMS12_PAGE::button_previous_page_id, GUI_ITEMS12_PAGE::button_next_page_id, GUI_ITEMS12_PAGE::item_slots);
    break;
  default:
    break; // Not showing.
  }
}

void MediaSourcePage::_handle_touch_event(nextion_event_touch_t *touch_data, uint8_t button_back_id, uint8_t button_previous_page_id, uint8_t button_next_page_id, const GUI_ITEMS_PAGE_ITEM_DATA *item_slots) {
  ESP_LOGD("MediaSourcePage", "Touch component %d, pressed %s", touch_data->component_id, touch_data->pressed ? "Yes" : "No");
  if (!touch_data->pressed) {
    return; // The items pages act on press, like EntitiesPage.
  }

  // A source can be picked by its icon or by its name.
  for (size_t slot = 0; slot < MediaSourcePage::_page_type; slot++) {
    if (touch_data->component_id == item_slots[slot].button_id || touch_data->component_id == item_slots[slot].label_id) {
      size_t source_index = MediaSourcePage::_first_source_index + slot;
      if (source_index < MediaSourcePage::_sources.size()) {
        MediaSourcePage::_select_source(source_index);
      }
      return;
    }
  }

  if (touch_data->component_id == button_back_id) {
    MediaSourcePage::_return_to_media_player();
  } else if (touch_data->component_id == button_previous_page_id) {
    if (MediaSourcePage::_first_source_index >= MediaSourcePage::_page_type) {
      MediaSourcePage::_first_source_index -= MediaSourcePage::_page_type;
      MediaSourcePage::_update_display();
    }
  } else if (touch_data->component_id == button_next_page_id) {
    if (MediaSourcePage::_first_source_index + MediaSourcePage::_page_type < MediaSourcePage::_sources.size()) {
      MediaSourcePage::_first_source_index += MediaSourcePage::_page_type;
      MediaSourcePage::_update_display();
    }
  }
}

void MediaSourcePage::_select_source(size_t source_index) {
  MediaSourcePage::_current_source = MediaSourcePage::_sources[source_index];
  (MediaSourcePage::_showing_source_player ? hard_coded_source_player_current_source : hard_coded_player_current_source) = MediaSourcePage::_current_source;
  // TODO: Send the selection, by name, once the protobuf has the commands (NSPanelManager PR #396).
  ESP_LOGI("MediaSourcePage", "Selected source '%s'. Would send %s, not sent to the manager yet.", MediaSourcePage::_current_source.c_str(),
           MediaSourcePage::_showing_source_player ? "select_source_player_source" : "select_player_source");
  MediaSourcePage::_return_to_media_player();
}

void MediaSourcePage::_return_to_media_player() {
  EntityPage::return_to_media_player(MediaSourcePage::_media_player_state_topic);
}
