#include <BackgroundManager.hpp>
#include <BuzzerManager.hpp>
#include <ConfigManager.hpp>
#include <DisplayManager.hpp>
#include <EntitiesPage.hpp>
#include <EntityPage.hpp>
#include <HomePage.hpp>
#include <InterfaceManager.hpp>
#include <MqttManager.hpp>
#include <NSPM_ConfigManager.hpp>
#include <RoomManager.hpp>
#include <RoomManager_event.hpp>
#include <cstring>
#include <esp_log.h>
#include <lvgl.h>
#include <string>

LV_FONT_DECLARE(font_entity_icons_48);
LV_FONT_DECLARE(font_entity_icons_28);
LV_FONT_DECLARE(font_screensaver_text_18);
LV_FONT_DECLARE(font_screensaver_text_15);

// ---------------------------------------------------------------------------
// LVGL user interface. Every function in here must be called with the LVGL
// lock held (the event callbacks run on the LVGL task, which holds it).
//
// The page shows 4, 8 or 12 slots, as the manager decides per page, filled row
// by row in two columns. With 4 slots the icon is above the name, split by a
// line; with 8 and 12 the icon is left of the name. The icon fonts hold Material
// Design Icons on the characters the manager sends (see the manager's
// entity_icons.hpp), so a slot's icon string is shown as is.
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t COLOR_BACKGROUND = 0xFFFFFF;
constexpr uint32_t COLOR_PANEL = 0x000000;
constexpr lv_opa_t PANEL_OPA = LV_OPA_60;
constexpr uint32_t COLOR_FOREGROUND = 0xFFFFFF;
constexpr uint32_t COLOR_PROGRESS = 0xFFC000;

// Layout, in screen pixels
constexpr int32_t MARGIN = 8;
constexpr int32_t RADIUS = 8;
constexpr int32_t HEADER_Y = 6;
constexpr int32_t HEADER_H = 41;
constexpr int32_t HEADER_W = DisplayManager::LANDSCAPE_WIDTH - 2 * MARGIN;
constexpr int32_t HEADER_BACK_W = 44;     // Touch area of the back chevron, up to the separator
constexpr int32_t HEADER_PREVIOUS_W = 70; // Touch area of the previous page chevron, right of the separator
constexpr int32_t HEADER_NEXT_W = 70;     // Touch area of the next page chevron
constexpr int32_t SAVE_PROGRESS_X = 126;  // Scene save progress bar, under the header text
constexpr int32_t SAVE_PROGRESS_W = 186;
constexpr int32_t GRID_Y = HEADER_Y + HEADER_H + 6;
constexpr int32_t GRID_W = HEADER_W;
constexpr int32_t GRID_H = DisplayManager::LANDSCAPE_HEIGHT - GRID_Y - 6;
constexpr int32_t GRID_COLUMNS = 2;
constexpr int32_t MAX_SLOTS = 12;
constexpr int32_t ICON_AREA_W = 52; // With 8 and 12 slots: width of the icon area on the left of a slot

struct Slot {
  lv_obj_t *box;
  lv_obj_t *icon;
  lv_obj_t *name;
  lv_color_t icon_color;         // pco from the manager
  lv_color_t icon_pressed_color; // pco2 from the manager
};

struct {
  lv_obj_t *screen = nullptr;
  lv_obj_t *header_text;
  lv_obj_t *save_progress;
  lv_obj_t *grid;
  int32_t page_type = 0; // Slots the grid is built for: 4, 8 or 12, or 0 when not built
  Slot slots[MAX_SLOTS];
} ui;

lv_color_t color_from_rgb565(uint16_t value) {
  return lv_color_make((value >> 11) << 3, ((value >> 5) & 0x3F) << 2, (value & 0x1F) << 3);
}

lv_obj_t *create_box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color, lv_opa_t opa, int32_t radius) {
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_set_scrollable(box, false);
  lv_obj_set_clickable(box, false);
  lv_obj_set_style_bg_color(box, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(box, opa, 0);
  lv_obj_set_style_radius(box, radius, 0);
  lv_obj_set_pos(box, x, y);
  lv_obj_set_size(box, w, h);
  return box;
}

lv_obj_t *create_label(lv_obj_t *parent, const lv_font_t *font, const char *text) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(COLOR_FOREGROUND), 0);
  lv_label_set_text(label, text);
  return label;
}

// --- Touch input. Only queued here, see EntitiesPage::InputEvent ---
void header_event(lv_event_t *e) {
  auto type = static_cast<EntitiesPage::InputEvent::Type>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  EntitiesPage::queue_input({type, 0});
}

// user_data: the slot, shifted left by one, with bit 0 set for the icon area.
void slot_event(lv_event_t *e) {
  intptr_t data = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
  uint8_t slot = data >> 1;
  bool icon = data & 1;
  if (slot >= MAX_SLOTS || ui.slots[slot].icon == nullptr) {
    return;
  }

  switch (lv_event_get_code(e)) {
  case LV_EVENT_PRESSED:
    if (icon) {
      lv_obj_set_style_text_color(ui.slots[slot].icon, ui.slots[slot].icon_pressed_color, 0);
      EntitiesPage::queue_input({EntitiesPage::InputEvent::ICON_PRESSED, slot});
    }
    break;
  case LV_EVENT_RELEASED:
  case LV_EVENT_PRESS_LOST:
    if (icon) {
      lv_obj_set_style_text_color(ui.slots[slot].icon, ui.slots[slot].icon_color, 0);
      EntitiesPage::queue_input({EntitiesPage::InputEvent::ICON_RELEASED, slot});
    }
    break;
  case LV_EVENT_CLICKED:
    EntitiesPage::queue_input({icon ? EntitiesPage::InputEvent::ICON_CLICKED : EntitiesPage::InputEvent::NAME_CLICKED, slot});
    break;
  default:
    break;
  }
}

// An invisible touch area. Create it after its parent's other children, so that it is on top of them.
void create_hit_area(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, lv_event_cb_t callback, intptr_t user_data) {
  lv_obj_t *area = lv_obj_create(parent);
  lv_obj_remove_style_all(area);
  lv_obj_set_scrollable(area, false);
  lv_obj_set_clickable(area, true);
  lv_obj_set_pos(area, x, y);
  lv_obj_set_size(area, w, h);
  lv_obj_add_event_cb(area, callback, LV_EVENT_ALL, reinterpret_cast<void *>(user_data));
}

void header_hit_area_event(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    header_event(e);
  }
}

void build_screen() {
  if (ui.screen != nullptr) {
    return;
  }

  ui.screen = lv_obj_create(NULL);
  lv_obj_set_scrollable(ui.screen, false);
  lv_obj_set_style_bg_color(ui.screen, lv_color_hex(COLOR_BACKGROUND), 0);
  lv_obj_set_style_bg_opa(ui.screen, LV_OPA_COVER, 0);
  BackgroundManager::attach(ui.screen);

  // Header: back to the home page | previous page   title   next page
  lv_obj_t *header = create_box(ui.screen, MARGIN, HEADER_Y, HEADER_W, HEADER_H, COLOR_PANEL, PANEL_OPA, RADIUS);
  lv_obj_t *back = create_label(header, &lv_font_montserrat_14, LV_SYMBOL_LEFT);
  lv_obj_align(back, LV_ALIGN_LEFT_MID, 16, 0);
  lv_obj_t *separator = create_box(header, HEADER_BACK_W, 10, 1, HEADER_H - 20, COLOR_FOREGROUND, LV_OPA_40, 0);
  (void)separator;
  lv_obj_t *previous = create_label(header, &lv_font_montserrat_14, LV_SYMBOL_LEFT);
  lv_obj_align(previous, LV_ALIGN_LEFT_MID, HEADER_BACK_W + 16, 0);
  lv_obj_t *next = create_label(header, &lv_font_montserrat_14, LV_SYMBOL_RIGHT);
  lv_obj_align(next, LV_ALIGN_RIGHT_MID, -16, 0);

  ui.header_text = create_label(header, &font_screensaver_text_18, "");
  lv_label_set_long_mode(ui.header_text, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_style_text_align(ui.header_text, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(ui.header_text, HEADER_W - HEADER_BACK_W - HEADER_PREVIOUS_W - HEADER_NEXT_W);
  lv_obj_align(ui.header_text, LV_ALIGN_CENTER, (HEADER_BACK_W + HEADER_PREVIOUS_W - HEADER_NEXT_W) / 2, 0);

  ui.save_progress = create_box(header, SAVE_PROGRESS_X, HEADER_H - 5, 0, 3, COLOR_PROGRESS, LV_OPA_COVER, 1);
  lv_obj_set_hidden(ui.save_progress, true);

  create_hit_area(header, 0, 0, HEADER_BACK_W, HEADER_H, header_hit_area_event, EntitiesPage::InputEvent::BACK);
  create_hit_area(header, HEADER_BACK_W, 0, HEADER_PREVIOUS_W, HEADER_H, header_hit_area_event, EntitiesPage::InputEvent::PREVIOUS_PAGE);
  create_hit_area(header, HEADER_W - HEADER_NEXT_W, 0, HEADER_NEXT_W, HEADER_H, header_hit_area_event, EntitiesPage::InputEvent::NEXT_PAGE);

  ui.grid = create_box(ui.screen, MARGIN, GRID_Y, GRID_W, GRID_H, COLOR_PANEL, LV_OPA_TRANSP, 0);
}

// Build the slots for a page of 4, 8 or 12 entities, replacing the previous ones.
void build_grid(int32_t page_type) {
  if (ui.page_type == page_type) {
    return;
  }
  lv_obj_clean(ui.grid);
  memset(ui.slots, 0, sizeof(ui.slots));
  ui.page_type = page_type;
  if (page_type == 0) {
    return;
  }

  const int32_t rows = page_type / GRID_COLUMNS;
  const int32_t gap = page_type == 4 ? 7 : 6;
  const int32_t box_w = (GRID_W - (GRID_COLUMNS - 1) * gap) / GRID_COLUMNS;
  const int32_t box_h = (GRID_H - (rows - 1) * gap) / rows;
  const bool icon_above_name = page_type == 4;
  const lv_font_t *icon_font = icon_above_name ? &font_entity_icons_48 : &font_entity_icons_28;
  const lv_font_t *name_font = page_type == 12 ? &font_screensaver_text_15 : &font_screensaver_text_18;

  for (int32_t i = 0; i < page_type; i++) {
    Slot &slot = ui.slots[i];
    slot.box = create_box(ui.grid, (i % GRID_COLUMNS) * (box_w + gap), (i / GRID_COLUMNS) * (box_h + gap), box_w, box_h, COLOR_PANEL, PANEL_OPA, RADIUS);
    slot.icon = create_label(slot.box, icon_font, "");
    slot.name = create_label(slot.box, name_font, "");
    lv_label_set_long_mode(slot.name, LV_LABEL_LONG_MODE_DOTS);
    slot.icon_color = lv_color_hex(COLOR_FOREGROUND);
    slot.icon_pressed_color = slot.icon_color;

    intptr_t icon_data = (i << 1) | 1;
    intptr_t name_data = i << 1;
    if (icon_above_name) {
      // Icon in the top half, name in the bottom half, with a line between them.
      int32_t half = box_h / 2;
      create_box(slot.box, 16, half, box_w - 32, 1, COLOR_FOREGROUND, LV_OPA_40, 0);
      lv_obj_align(slot.icon, LV_ALIGN_CENTER, 0, -half / 2);
      lv_obj_set_width(slot.name, box_w - 32);
      lv_obj_align(slot.name, LV_ALIGN_CENTER, 0, half / 2);
      create_hit_area(slot.box, 0, 0, box_w, half, slot_event, icon_data);
      create_hit_area(slot.box, 0, half, box_w, box_h - half, slot_event, name_data);
    } else {
      // Icon on the left, name on the right.
      lv_obj_align(slot.icon, LV_ALIGN_LEFT_MID, (ICON_AREA_W - icon_font->line_height) / 2 + 4, 0);
      lv_obj_set_width(slot.name, box_w - ICON_AREA_W - 10);
      lv_obj_align(slot.name, LV_ALIGN_LEFT_MID, ICON_AREA_W, 0);
      create_hit_area(slot.box, 0, 0, ICON_AREA_W, box_h, slot_event, icon_data);
      create_hit_area(slot.box, ICON_AREA_W, 0, box_w - ICON_AREA_W, box_h, slot_event, name_data);
    }
  }
}

void set_text(lv_obj_t *label, const char *text) {
  if (strcmp(lv_label_get_text(label), text) != 0) { // Unchanged text would still redraw the label
    lv_label_set_text(label, text);
  }
}

void show_slot(Slot &slot, const NSPanelRoomEntitiesPage__EntitySlot *data) {
  set_text(slot.icon, data != nullptr ? data->icon : "");
  set_text(slot.name, data != nullptr ? data->name : "");
  slot.icon_color = data != nullptr ? color_from_rgb565(data->pco) : lv_color_hex(COLOR_FOREGROUND);
  slot.icon_pressed_color = data != nullptr ? color_from_rgb565(data->pco2) : slot.icon_color;
  lv_obj_set_style_text_color(slot.icon, slot.icon_color, 0);
}

// Show scene saving progress, 0-100, or hide it with a negative value.
void show_save_progress(int32_t percent) {
  lv_obj_set_hidden(ui.save_progress, percent < 0);
  if (percent >= 0) {
    lv_obj_set_width(ui.save_progress, SAVE_PROGRESS_W * percent / 100);
  }
}
} // namespace

// ---------------------------------------------------------------------------
// EntitiesPage
// ---------------------------------------------------------------------------
void EntitiesPage::show(display_type_t display_type) {
  esp_log_level_set("EntitiesPage", ConfigManager::log_level);
  EntitiesPage::_display_type = display_type;

  if (EntitiesPage::_input_queue == NULL) {
    EntitiesPage::_input_queue = xQueueCreate(8, sizeof(InputEvent));
    if (EntitiesPage::_input_queue == NULL || xTaskCreatePinnedToCore(EntitiesPage::_task_handle_input, "entities_input", 4096, NULL, 3, NULL, 1) != pdPASS) {
      ESP_LOGE("EntitiesPage", "Failed to start input task. Entities page will not react to touch!");
    }
  }

  std::shared_ptr<PageHandlers> handlers = InterfaceManager::begin_page(EntitiesPage::unshow);

  {
    // Start empty: the page to show arrives in a ROOM_ENTITIES_PAGE_UPDATED event, and until then
    // the slots of the previously shown page must not show.
    DisplayManager::Lock lock;
    build_screen();
    build_grid(0);
    set_text(ui.header_text, "");
    show_save_progress(-1);
    lv_screen_load(ui.screen);
  }

  if (handlers->add_to(RoomManager::event_loop(), ROOMMANAGER_EVENT, ESP_EVENT_ANY_ID, EntitiesPage::_handle_roommanager_event) != ESP_OK) [[unlikely]] {
    ESP_LOGE("EntitiesPage", "Failed to register event handler for EntitiesPage to RoomManager. Will return on HomePage.");
    HomePage::show();
    return;
  }

  // If current room has an entities page, go to it and if not, show the next available entities page.
  switch (display_type) {
  case display_type_t::ENTITIES:
    RoomManager::go_to_first_entities_page();
    break;

  case display_type_t::SCENES:
    RoomManager::go_to_first_scenes_page();
    break;

  case display_type_t::GLOBAL_SCENES:
    RoomManager::go_to_first_global_scenes_page();
    break;

  default:
    ESP_LOGE("EntitiesPage", "Unknown display type! Will return to HomePage.");
    HomePage::show();
    break;
  }
}

void EntitiesPage::unshow() {
  // Event handlers stop with the page: InterfaceManager only dispatches to the current page.
  EntitiesPage::_current_entities_page.set(nullptr);
  EntitiesPage::_save_scene = false;
}

void EntitiesPage::queue_input(InputEvent event) {
  if (EntitiesPage::_input_queue != NULL && xQueueSend(EntitiesPage::_input_queue, &event, 0) != pdTRUE) {
    ESP_LOGW("EntitiesPage", "Input queue full, dropped touch input.");
  }
}

void EntitiesPage::_update_display(const std::shared_ptr<NSPanelRoomEntitiesPage> &page) {
  if (page == nullptr) {
    ESP_LOGE("EntitiesPage", "Tried to show RoomEntitiesPage but no _current_room_status has been set. Returning to HomePage.");
    InterfaceManager::request_show_home_page(); // May run on the RoomManager loop; see InterfaceManager::_task_page_transitions.
    return;
  }
  if (page->page_type != 4 && page->page_type != 8 && page->page_type != 12) {
    ESP_LOGE("EntitiesPage", "Unknown entities page type! Type: %ld. Will navigate to home page.", page->page_type);
    InterfaceManager::request_show_home_page(); // May run on the RoomManager loop; see InterfaceManager::_task_page_transitions.
    return;
  }

  DisplayManager::Lock lock;
  if (ui.screen == nullptr) {
    return;
  }
  build_grid(page->page_type);
  if (EntitiesPage::_save_scene_task_handle == NULL) { // While saving, the header shows the save result
    set_text(ui.header_text, page->header_text);
  }
  for (int32_t i = 0; i < page->page_type; i++) {
    show_slot(ui.slots[i], EntitiesPage::_get_entity_in_slot(page, i));
  }
}

void EntitiesPage::_handle_roommanager_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id == roommanager_event_t::ROOM_ENTITIES_PAGE_UPDATED) {
    ESP_LOGD("EntitiesPage", "Received new data. Will update display.");
    std::shared_ptr<NSPanelRoomEntitiesPage> page;
    if (RoomManager::get_current_room_entities_page_status(&page) == ESP_OK) [[likely]] {
      EntitiesPage::_current_entities_page.set(page);
      EntitiesPage::_update_display(page);
    } else {
      ESP_LOGE("EntitiesPage", "Got new entity page data event but couldn't get new entity page data!");
      if (EntitiesPage::_current_entities_page.get() == nullptr) {
        ESP_LOGE("EntitiesPage", "Currently not showing entities page and we failed to get data. Will go back to HomePage.");
        // Not HomePage::show(): this runs on the RoomManager loop, see InterfaceManager::_task_page_transitions.
        InterfaceManager::request_show_home_page();
      }
    }
  }
}

void EntitiesPage::_task_handle_input(void *param) {
  InputEvent event;
  for (;;) {
    if (xQueueReceive(EntitiesPage::_input_queue, &event, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    if (!InterfaceManager::is_current_page(EntitiesPage::unshow)) {
      continue; // Touched just before another page was shown
    }

    switch (event.type) {
    case InputEvent::BACK:
      HomePage::show();
      continue;
    case InputEvent::PREVIOUS_PAGE:
      EntitiesPage::_go_to_previous_page();
      continue;
    case InputEvent::NEXT_PAGE:
      EntitiesPage::_go_to_next_page();
      continue;
    default:
      break;
    }

    // Slot input. Take our own reference for the whole event, see _current_entities_page.
    std::shared_ptr<NSPanelRoomEntitiesPage> page = EntitiesPage::_current_entities_page.get();
    if (page == nullptr) {
      continue;
    }
    NSPanelRoomEntitiesPage__EntitySlot *entity = EntitiesPage::_get_entity_in_slot(page, event.slot);
    if (entity == nullptr) {
      continue; // Empty slot
    }

    bool scenes = EntitiesPage::_display_type == display_type_t::SCENES || EntitiesPage::_display_type == display_type_t::GLOBAL_SCENES;
    if (scenes) {
      // Scenes: the name activates the scene, holding the icon saves it.
      switch (event.type) {
      case InputEvent::NAME_CLICKED:
        BuzzerManager::raise_event_for_touch(buzzer_event_t::SCENE_ACTIVATED);
        EntitiesPage::_send_entity_toggle_command_to_manager(page->id, event.slot);
        break;
      case InputEvent::ICON_PRESSED:
        if (entity->can_save_scene && EntitiesPage::_save_scene_task_handle == NULL) {
          ESP_LOGD("EntitiesPage", "Entity in slot %u can save scene.", event.slot);
          EntitiesPage::_save_scene = true;
          int32_t *scene_slot = new int32_t(event.slot);
          if (xTaskCreatePinnedToCore(EntitiesPage::_task_save_scene_progress, "save_scene", 4096, (void *)scene_slot, 1, &EntitiesPage::_save_scene_task_handle, 1) != pdPASS) {
            delete scene_slot;
            EntitiesPage::_save_scene_task_handle = NULL;
          }
        }
        break;
      case InputEvent::ICON_RELEASED:
        EntitiesPage::_save_scene = false; // Released before the scene was saved
        break;
      default:
        break;
      }
    } else {
      // Entities: the icon toggles the entity, the name opens it.
      switch (event.type) {
      case InputEvent::ICON_CLICKED:
        EntitiesPage::_handle_entity_button_pressed(page, event.slot);
        break;
      case InputEvent::NAME_CLICKED:
        if (entity->mqtt_state_topic[0] != '\0') {
          EntityPage::show(entity->mqtt_state_topic);
        }
        break;
      default:
        break;
      }
    }
  }
}

void EntitiesPage::_go_to_previous_page() {
  if (EntitiesPage::_display_type == display_type_t::SCENES) {
    RoomManager::go_to_previous_scenes_page();
  } else if (EntitiesPage::_display_type == display_type_t::GLOBAL_SCENES) {
    RoomManager::go_to_previous_global_scenes_page();
  } else {
    RoomManager::go_to_previous_entities_page();
  }
}

void EntitiesPage::_go_to_next_page() {
  if (EntitiesPage::_display_type == display_type_t::SCENES) {
    RoomManager::go_to_next_scenes_page();
  } else if (EntitiesPage::_display_type == display_type_t::GLOBAL_SCENES) {
    RoomManager::go_to_next_global_scenes_page();
  } else {
    RoomManager::go_to_next_entities_page();
  }
}

NSPanelRoomEntitiesPage__EntitySlot *EntitiesPage::_get_entity_in_slot(const std::shared_ptr<NSPanelRoomEntitiesPage> &page, int32_t slot) {
  for (int i = 0; i < page->n_entities; i++) {
    if (page->entities[i] != nullptr && page->entities[i]->room_view_position == slot) {
      return page->entities[i];
    }
  }
  return nullptr;
}

void EntitiesPage::_handle_entity_button_pressed(const std::shared_ptr<NSPanelRoomEntitiesPage> &page, int32_t slot) {
  NSPanelRoomEntitiesPage__EntitySlot *entity = EntitiesPage::_get_entity_in_slot(page, slot);
  // Media players can't be toggled, open their control page instead.
  if (entity != nullptr && entity->type == NSPANEL_ROOM_ENTITIES_PAGE__ENTITY_SLOT__ENTITY_TYPE__ENTITY_TYPE_MEDIA_PLAYER && entity->mqtt_state_topic[0] != '\0') {
    EntityPage::show(entity->mqtt_state_topic);
    return;
  }
  EntitiesPage::_send_entity_toggle_command_to_manager(page->id, slot);
}

void EntitiesPage::_send_entity_toggle_command_to_manager(uint32_t entity_page_id, uint32_t entity_slot) {
  NSPanelMQTTManagerCommand__ToggleEntityFromEntitiesPage toggle_cmd = NSPANEL_MQTTMANAGER_COMMAND__TOGGLE_ENTITY_FROM_ENTITIES_PAGE__INIT;
  toggle_cmd.entity_page_id = entity_page_id;
  toggle_cmd.entity_slot = entity_slot;

  ESP_LOGD("EntitiesPage", "Sending command to toggle entity in slot %ld from entity page with ID %ld.", entity_slot, entity_page_id);

  NSPanelMQTTManagerCommand cmd = NSPANEL_MQTTMANAGER_COMMAND__INIT;
  cmd.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_TOGGLE_ENTITY_FROM_ENTITIES_PAGE;
  cmd.toggle_entity_from_entities_page = &toggle_cmd;
  cmd.nspanel_id = NSPM_ConfigManager::get_nspanel_id();

  uint32_t packed_length = nspanel_mqttmanager_command__get_packed_size(&cmd);
  std::vector<uint8_t> buffer(packed_length); // Use vector for automatic cleanup of data when going out of scope
  size_t packed_data_size = nspanel_mqttmanager_command__pack(&cmd, buffer.data());
  if (packed_data_size == packed_length) {
    if (MqttManager::publish(NSPM_ConfigManager::get_manager_command_topic(), (const char *)buffer.data(), packed_length, false) != ESP_OK) {
      ESP_LOGE("EntitiesPage", "Failed to send toggle command!");
    }
  } else {
    ESP_LOGE("EntitiesPage", "Failed to serialize toggle command!");
  }
}

void EntitiesPage::_task_save_scene_progress(void *scene_slot) {
  int32_t scene_slot_int = *((int32_t *)scene_slot);
  delete (int32_t *)scene_slot;

  ESP_LOGD("EntitiesPage", "Starting save of scene in slot %ld", scene_slot_int);

  // Hold for 3 seconds to save, showing the progress under the header text. Releasing cancels.
  for (int i = 0; i <= 100 && EntitiesPage::_save_scene; i += 2) {
    {
      DisplayManager::Lock lock;
      show_save_progress(i);
    }
    vTaskDelay(pdMS_TO_TICKS(3000 / (100 / 2)));
  }
  {
    DisplayManager::Lock lock;
    show_save_progress(-1);
  }

  std::shared_ptr<NSPanelRoomEntitiesPage> page = EntitiesPage::_current_entities_page.get();
  if (EntitiesPage::_save_scene && page == nullptr) {
    ESP_LOGW("EntitiesPage", "Left the entities page while saving scene in slot %ld. Will not save it.", scene_slot_int);
  } else if (EntitiesPage::_save_scene) {
    // Send command to save scene.
    NSPanelMQTTManagerCommand__SaveSceneCommand save_command = NSPANEL_MQTTMANAGER_COMMAND__SAVE_SCENE_COMMAND__INIT;
    save_command.entity_page_id = page->id;
    save_command.entity_slot = scene_slot_int;

    ESP_LOGD("EntitiesPage", "Sending command to save scene in slot %ld from entity page with ID %ld.", scene_slot_int, page->id);

    NSPanelMQTTManagerCommand cmd = NSPANEL_MQTTMANAGER_COMMAND__INIT;
    cmd.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_SAVE_SCENE_COMMAND;
    cmd.save_scene_command = &save_command;
    cmd.nspanel_id = NSPM_ConfigManager::get_nspanel_id();

    uint32_t packed_length = nspanel_mqttmanager_command__get_packed_size(&cmd);
    std::vector<uint8_t> buffer(packed_length); // Use vector for automatic cleanup of data when going out of scope
    size_t packed_data_size = nspanel_mqttmanager_command__pack(&cmd, buffer.data());
    bool saved = false;
    if (packed_data_size != packed_length) {
      ESP_LOGE("EntitiesPage", "Failed to serialize save command!");
    } else if (MqttManager::publish(NSPM_ConfigManager::get_manager_command_topic(), (const char *)buffer.data(), packed_length, false) != ESP_OK) {
      ESP_LOGE("EntitiesPage", "Failed to send save command!");
    } else {
      saved = true;
      BuzzerManager::raise_event(buzzer_event_t::SCENE_SAVED);
    }

    // Tell the user how it went for a second, in place of the header text.
    {
      DisplayManager::Lock lock;
      set_text(ui.header_text, saved ? "Scene saved" : "Save failed");
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    std::shared_ptr<NSPanelRoomEntitiesPage> current = EntitiesPage::_current_entities_page.get();
    if (current != nullptr) {
      DisplayManager::Lock lock;
      set_text(ui.header_text, current->header_text);
    }
  }

  // Task finished. Reset ptr and exit task.
  EntitiesPage::_save_scene_task_handle = NULL;
  vTaskDelete(NULL);
}
