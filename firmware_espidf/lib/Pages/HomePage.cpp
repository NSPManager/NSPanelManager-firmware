#include <BackgroundManager.hpp>
#include <ConfigManager.hpp>
#include <DisplayManager.hpp>
#include <EntitiesPage.hpp>
#include <HomePage.hpp>
#include <InterfaceManager.hpp>
#include <MqttManager.hpp>
#include <NSPM_ConfigManager.hpp>
#include <NSPM_ConfigManager_event.hpp>
#include <RoomManager.hpp>
#include <RoomManager_event.hpp>
#include <cstring>
#include <esp_log.h>
#include <lvgl.h>
#include <memory>
#include <protobuf_nspanel.pb-c.h>
#include <vector>

// ---------------------------------------------------------------------------
// LVGL user interface. Every function in here must be called with the LVGL
// lock held (the event callbacks run on the LVGL task, which holds it).
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t COLOR_BACKGROUND = 0xFFFFFF;
constexpr uint32_t COLOR_PANEL = 0x000000;
constexpr lv_opa_t PANEL_OPA = LV_OPA_60;
constexpr uint32_t COLOR_FOREGROUND = 0xFFFFFF;
constexpr uint32_t COLOR_LIGHT_ON = 0xFFEE33;
constexpr uint32_t COLOR_LIGHT_OFF = 0x9E9E9E;
constexpr uint32_t COLOR_HIGHLIGHT = 0xFFC000; // Special mode (ceiling or table lights only)
constexpr uint32_t COLOR_WARM = 0xFFC000;
constexpr uint32_t COLOR_COLD = 0x8FD3F5;

// Layout, in screen pixels
constexpr int32_t MARGIN = 8;
constexpr int32_t GAP = 7;
constexpr int32_t RADIUS = 8;
constexpr int32_t TOP_BAR_Y = 6;
constexpr int32_t TOP_BAR_H = 41;
constexpr int32_t CARD_Y = 54;
constexpr int32_t CARD_H = 210;
constexpr int32_t CARD_W = (DisplayManager::WIDTH - 2 * MARGIN - 3 * GAP) / 4;
constexpr int32_t CARD_CENTER_X = CARD_W / 2;
constexpr int32_t VALUE_LABEL_Y = 152; // Brightness value under the ceiling and table light icons
constexpr int32_t ICON_CENTER_Y = 106; // Sun and color ring on the slider cards
constexpr int32_t BOTTOM_BAR_Y = 271;
constexpr int32_t BOTTOM_BAR_H = 42;
constexpr int32_t BOTTOM_BAR_W = (DisplayManager::WIDTH - 2 * MARGIN - GAP) / 2;
constexpr int32_t BOTTOM_BAR_TEXT_X = 46; // Left of the room/mode text, right of the chevron
constexpr int32_t SLIDER_PAD = 6;         // Space between the card edge and the slider line at 0 and 100
constexpr int32_t SLIDER_LINE_H = 3;
constexpr int32_t SLIDER_RANGE = CARD_H - 2 * SLIDER_PAD - SLIDER_LINE_H;

struct {
  lv_obj_t *screen = nullptr;
  lv_obj_t *ceiling_card = nullptr;
  lv_obj_t *ceiling_label = nullptr;
  lv_obj_t *table_card = nullptr;
  lv_obj_t *table_label = nullptr;
  lv_obj_t *brightness_card = nullptr;
  lv_obj_t *brightness_line = nullptr;
  lv_obj_t *color_temperature_card = nullptr;
  lv_obj_t *color_temperature_line = nullptr;
  lv_obj_t *room_label = nullptr;
  lv_obj_t *mode_label = nullptr;

  bool ceiling_on = false;
  bool table_on = false;
  // Set while the user drags a slider, so that status updates do not move it under their finger.
  bool brightness_dragging = false;
  bool color_temperature_dragging = false;
  int32_t brightness_value = 0;
  int32_t color_temperature_value = 0;
} ui;

int32_t card_x(int index) {
  return MARGIN + index * (CARD_W + GAP);
}

int32_t slider_value_from_y(int32_t y) {
  int32_t value = 100 - ((y - SLIDER_PAD) * 100) / SLIDER_RANGE;
  return value < 0 ? 0 : (value > 100 ? 100 : value);
}

void set_slider_line(lv_obj_t *line, int32_t value) {
  lv_obj_set_y(line, SLIDER_PAD + ((100 - value) * SLIDER_RANGE) / 100);
}

// --- Drawing helpers, in screen coordinates ---
void draw_line(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t width, uint32_t color) {
  lv_draw_line_dsc_t dsc;
  lv_draw_line_dsc_init(&dsc);
  dsc.color = lv_color_hex(color);
  dsc.width = width;
  dsc.round_start = 1;
  dsc.round_end = 1;
  dsc.p1.x = x1;
  dsc.p1.y = y1;
  dsc.p2.x = x2;
  dsc.p2.y = y2;
  lv_draw_line(layer, &dsc);
}

void draw_arc(lv_layer_t *layer, int32_t cx, int32_t cy, int32_t radius, int32_t width, int32_t start, int32_t end, uint32_t color) {
  lv_draw_arc_dsc_t dsc;
  lv_draw_arc_dsc_init(&dsc);
  dsc.color = lv_color_hex(color);
  dsc.center.x = cx;
  dsc.center.y = cy;
  dsc.radius = radius;
  dsc.width = width;
  dsc.start_angle = start;
  dsc.end_angle = end;
  lv_draw_arc(layer, &dsc);
}

void draw_rect(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color, int32_t radius = 0) {
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_color = lv_color_hex(color);
  dsc.radius = radius;
  lv_area_t area = {x1, y1, x2, y2};
  lv_draw_rect(layer, &dsc, &area);
}

void draw_circle(lv_layer_t *layer, int32_t cx, int32_t cy, int32_t radius, uint32_t color) {
  draw_rect(layer, cx - radius, cy - radius, cx + radius, cy + radius, color, LV_RADIUS_CIRCLE);
}

// A lamp shade: the top half of a disc, with an optional straight part below it.
void draw_shade(lv_layer_t *layer, int32_t cx, int32_t cy, int32_t radius, int32_t skirt, uint32_t color) {
  draw_arc(layer, cx, cy, radius, radius, 180, 360, color);
  if (skirt > 0) {
    draw_rect(layer, cx - radius, cy, cx + radius, cy + skirt, color);
  }
}

// Origin of the object that is being drawn, in screen coordinates.
lv_point_t draw_origin(lv_event_t *e, lv_layer_t **layer) {
  lv_obj_t *obj = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
  *layer = lv_event_get_layer(e);
  lv_area_t coords;
  lv_obj_get_coords(obj, &coords);
  return {coords.x1, coords.y1};
}

// --- Icons, drawn on top of each card's background ---
void draw_ceiling_light_icon(lv_event_t *e) {
  lv_layer_t *layer;
  lv_point_t o = draw_origin(e, &layer);
  int32_t cx = o.x + CARD_CENTER_X;
  draw_line(layer, cx - 28, o.y + 74, cx + 28, o.y + 74, 2, COLOR_FOREGROUND); // Ceiling
  draw_circle(layer, cx, o.y + 79, 4, COLOR_FOREGROUND);                       // Ceiling rose
  draw_line(layer, cx, o.y + 79, cx, o.y + 110, 2, COLOR_FOREGROUND);          // Cord
  draw_shade(layer, cx, o.y + 122, 14, 12, ui.ceiling_on ? COLOR_LIGHT_ON : COLOR_LIGHT_OFF);
}

void draw_table_light_icon(lv_event_t *e) {
  lv_layer_t *layer;
  lv_point_t o = draw_origin(e, &layer);
  int32_t cx = o.x + CARD_CENTER_X;
  draw_line(layer, cx, o.y + 104, cx, o.y + 138, 3, COLOR_FOREGROUND);           // Pole
  draw_line(layer, cx - 27, o.y + 139, cx + 27, o.y + 139, 2, COLOR_FOREGROUND); // Table
  draw_shade(layer, cx, o.y + 104, 16, 0, ui.table_on ? COLOR_LIGHT_ON : COLOR_LIGHT_OFF);
}

void draw_brightness_icon(lv_event_t *e) {
  // Sun: a disc with eight rays. Ray directions as (x, y) * 1000.
  static constexpr int16_t rays[8][2] = {{1000, 0}, {707, 707}, {0, 1000}, {-707, 707}, {-1000, 0}, {-707, -707}, {0, -1000}, {707, -707}};
  lv_layer_t *layer;
  lv_point_t o = draw_origin(e, &layer);
  int32_t cx = o.x + CARD_CENTER_X;
  int32_t cy = o.y + ICON_CENTER_Y;
  draw_circle(layer, cx, cy, 8, COLOR_FOREGROUND);
  for (const auto &ray : rays) {
    draw_line(layer, cx + ray[0] * 12 / 1000, cy + ray[1] * 12 / 1000, cx + ray[0] * 18 / 1000, cy + ray[1] * 18 / 1000, 2, COLOR_FOREGROUND);
  }
}

void draw_color_temperature_icon(lv_event_t *e) {
  // Ring, warm on the left half and cold on the right half.
  lv_layer_t *layer;
  lv_point_t o = draw_origin(e, &layer);
  int32_t cx = o.x + CARD_CENTER_X;
  int32_t cy = o.y + ICON_CENTER_Y;
  draw_arc(layer, cx, cy, 16, 5, 90, 270, COLOR_WARM);
  draw_arc(layer, cx, cy, 16, 5, 270, 90, COLOR_COLD);
}

void draw_top_bar_icon(lv_event_t *e) {
  // Three sliders
  lv_layer_t *layer;
  lv_point_t o = draw_origin(e, &layer);
  static constexpr int32_t knob_y[3] = {14, 26, 18};
  for (int i = 0; i < 3; i++) {
    int32_t x = o.x + 38 + i * 10;
    draw_line(layer, x, o.y + 10, x, o.y + TOP_BAR_H - 10, 2, COLOR_FOREGROUND);
    draw_line(layer, x - 4, o.y + knob_y[i], x + 4, o.y + knob_y[i], 3, COLOR_FOREGROUND);
  }
}

// --- Touch input. Only queued here, see HomePage::InputEvent ---
void light_button_event(lv_event_t *e) {
  bool ceiling = lv_event_get_current_target(e) == ui.ceiling_card;
  switch (lv_event_get_code(e)) {
  case LV_EVENT_PRESSED:
    HomePage::queue_input({ceiling ? HomePage::InputEvent::CEILING_BUTTON_PRESSED : HomePage::InputEvent::TABLE_BUTTON_PRESSED, 0});
    break;
  case LV_EVENT_RELEASED:
  case LV_EVENT_PRESS_LOST:
    HomePage::queue_input({ceiling ? HomePage::InputEvent::CEILING_BUTTON_RELEASED : HomePage::InputEvent::TABLE_BUTTON_RELEASED, 0});
    break;
  default:
    break;
  }
}

void slider_event(lv_event_t *e) {
  lv_obj_t *card = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
  bool brightness = card == ui.brightness_card;
  bool &dragging = brightness ? ui.brightness_dragging : ui.color_temperature_dragging;
  int32_t &value = brightness ? ui.brightness_value : ui.color_temperature_value;

  switch (lv_event_get_code(e)) {
  case LV_EVENT_PRESSED:
  case LV_EVENT_PRESSING: {
    lv_indev_t *indev = lv_indev_active();
    if (indev == nullptr) {
      return;
    }
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    lv_area_t coords;
    lv_obj_get_coords(card, &coords);
    dragging = true;
    value = slider_value_from_y(point.y - coords.y1);
    set_slider_line(brightness ? ui.brightness_line : ui.color_temperature_line, value);
    break;
  }
  case LV_EVENT_RELEASED:
  case LV_EVENT_PRESS_LOST:
    if (dragging) {
      dragging = false;
      HomePage::queue_input({brightness ? HomePage::InputEvent::BRIGHTNESS_SLIDER_SET : HomePage::InputEvent::COLOR_TEMPERATURE_SLIDER_SET, value});
    }
    break;
  default:
    break;
  }
}

void hit_area_event(lv_event_t *e) {
  auto type = static_cast<HomePage::InputEvent::Type>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  HomePage::queue_input({type, 0});
}

// --- Building the screen ---
lv_obj_t *create_panel(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, bool clickable) {
  lv_obj_t *panel = lv_obj_create(parent);
  lv_obj_remove_style_all(panel);
  lv_obj_set_scrollable(panel, false);
  lv_obj_set_clickable(panel, clickable);
  lv_obj_set_style_bg_color(panel, lv_color_hex(COLOR_PANEL), 0);
  lv_obj_set_style_bg_opa(panel, PANEL_OPA, 0);
  lv_obj_set_style_radius(panel, RADIUS, 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(COLOR_HIGHLIGHT), 0);
  lv_obj_set_style_border_width(panel, 0, 0);
  lv_obj_set_pos(panel, x, y);
  lv_obj_set_size(panel, w, h);
  return panel;
}

lv_obj_t *create_label(lv_obj_t *parent, const char *text) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(COLOR_FOREGROUND), 0);
  lv_label_set_text(label, text);
  return label;
}

// An invisible area that queues `type` when tapped. Create it after the parent's other
// children, so that it is on top of them. By default it is as high as its parent.
void create_hit_area(lv_obj_t *parent, int32_t x, int32_t w, HomePage::InputEvent::Type type, int32_t y = 0, int32_t h = lv_pct(100)) {
  lv_obj_t *area = lv_obj_create(parent);
  lv_obj_remove_style_all(area);
  lv_obj_set_scrollable(area, false);
  lv_obj_set_clickable(area, true);
  lv_obj_set_pos(area, x, y);
  lv_obj_set_size(area, w, h);
  lv_obj_add_event_cb(area, hit_area_event, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(type)));
}

lv_obj_t *create_slider_line(lv_obj_t *card) {
  lv_obj_t *line = lv_obj_create(card);
  lv_obj_remove_style_all(line);
  lv_obj_set_clickable(line, false);
  lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(line, lv_color_hex(COLOR_FOREGROUND), 0);
  lv_obj_set_size(line, CARD_W - 16, SLIDER_LINE_H);
  lv_obj_set_x(line, 8);
  set_slider_line(line, 0);
  return line;
}

lv_obj_t *create_bottom_bar(int index, lv_obj_t **label, bool with_separator) {
  lv_obj_t *bar = create_panel(ui.screen, MARGIN + index * (BOTTOM_BAR_W + GAP), BOTTOM_BAR_Y, BOTTOM_BAR_W, BOTTOM_BAR_H, false);

  lv_obj_t *chevron = create_label(bar, LV_SYMBOL_DOWN);
  lv_obj_align(chevron, LV_ALIGN_LEFT_MID, 16, 0);

  if (with_separator) {
    lv_obj_t *separator = lv_obj_create(bar);
    lv_obj_remove_style_all(separator);
    lv_obj_set_clickable(separator, false);
    lv_obj_set_style_bg_opa(separator, LV_OPA_40, 0);
    lv_obj_set_style_bg_color(separator, lv_color_hex(COLOR_FOREGROUND), 0);
    lv_obj_set_size(separator, 1, BOTTOM_BAR_H - 20);
    lv_obj_align(separator, LV_ALIGN_LEFT_MID, BOTTOM_BAR_TEXT_X - 2, 0);
  }

  *label = create_label(bar, "");
  lv_label_set_long_mode(*label, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_style_text_align(*label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(*label, BOTTOM_BAR_W - BOTTOM_BAR_TEXT_X - 8);
  lv_obj_align(*label, LV_ALIGN_LEFT_MID, BOTTOM_BAR_TEXT_X, 0);
  return bar;
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

  lv_obj_t *top_bar = create_panel(ui.screen, MARGIN, TOP_BAR_Y, DisplayManager::WIDTH - 2 * MARGIN, TOP_BAR_H, false);
  lv_obj_add_event_cb(top_bar, draw_top_bar_icon, LV_EVENT_DRAW_MAIN, nullptr);
  create_hit_area(top_bar, 0, 100, HomePage::InputEvent::SHOW_SCENES); // The sliders icon, with some room around it

  ui.ceiling_card = create_panel(ui.screen, card_x(0), CARD_Y, CARD_W, CARD_H, true);
  lv_obj_add_event_cb(ui.ceiling_card, draw_ceiling_light_icon, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_add_event_cb(ui.ceiling_card, light_button_event, LV_EVENT_ALL, nullptr);
  ui.ceiling_label = create_label(ui.ceiling_card, "0");
  lv_obj_align(ui.ceiling_label, LV_ALIGN_TOP_MID, 0, VALUE_LABEL_Y);

  ui.table_card = create_panel(ui.screen, card_x(1), CARD_Y, CARD_W, CARD_H, true);
  lv_obj_add_event_cb(ui.table_card, draw_table_light_icon, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_add_event_cb(ui.table_card, light_button_event, LV_EVENT_ALL, nullptr);
  ui.table_label = create_label(ui.table_card, "0");
  lv_obj_align(ui.table_label, LV_ALIGN_TOP_MID, 0, VALUE_LABEL_Y);

  ui.brightness_card = create_panel(ui.screen, card_x(2), CARD_Y, CARD_W, CARD_H, true);
  lv_obj_add_event_cb(ui.brightness_card, draw_brightness_icon, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_add_event_cb(ui.brightness_card, slider_event, LV_EVENT_ALL, nullptr);
  ui.brightness_line = create_slider_line(ui.brightness_card);

  ui.color_temperature_card = create_panel(ui.screen, card_x(3), CARD_Y, CARD_W, CARD_H, true);
  lv_obj_add_event_cb(ui.color_temperature_card, draw_color_temperature_icon, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_add_event_cb(ui.color_temperature_card, slider_event, LV_EVENT_ALL, nullptr);
  ui.color_temperature_line = create_slider_line(ui.color_temperature_card);

  // Room bar: the chevron goes to the next room, the room name opens the room's entities.
  lv_obj_t *room_bar = create_bottom_bar(0, &ui.room_label, true);
  create_hit_area(room_bar, BOTTOM_BAR_TEXT_X - 2, BOTTOM_BAR_W - BOTTOM_BAR_TEXT_X + 2, HomePage::InputEvent::SHOW_ROOM_ENTITIES);
  // Mode bar: anywhere switches between the current room and all rooms.
  lv_obj_t *mode_bar = create_bottom_bar(1, &ui.mode_label, false);
  create_hit_area(mode_bar, 0, BOTTOM_BAR_W, HomePage::InputEvent::NEXT_MODE);
  // The chevron is small, so it gets the whole bottom left corner of the screen: from the screen's
  // edges to the separator, and from below the light cards. Created last, so it is on top.
  constexpr int32_t corner_y = CARD_Y + CARD_H;
  create_hit_area(ui.screen, 0, MARGIN + BOTTOM_BAR_TEXT_X - 2, HomePage::InputEvent::NEXT_ROOM, corner_y, DisplayManager::HEIGHT - corner_y);
}

void set_label_text(lv_obj_t *label, const char *text) {
  if (strcmp(lv_label_get_text(label), text) != 0) { // Unchanged text would still redraw the label
    lv_label_set_text(label, text);
  }
}

void set_label_value(lv_obj_t *label, int32_t value) {
  char text[12];
  snprintf(text, sizeof(text), "%ld", (long)value);
  set_label_text(label, text);
}

void set_light_on(lv_obj_t *card, bool &current, bool on) {
  if (current != on) {
    current = on;
    lv_obj_invalidate(card); // Redraws the icon in its new color
  }
}

void set_highlighted(lv_obj_t *obj, bool highlighted) {
  lv_obj_set_style_border_width(obj, highlighted ? 2 : 0, 0);
}

void set_slider_line_color(lv_obj_t *line, bool highlighted) {
  lv_obj_set_style_bg_color(line, lv_color_hex(highlighted ? COLOR_HIGHLIGHT : COLOR_FOREGROUND), 0);
}

// The sliders only follow the lights when the value has changed enough, or reached an end:
// the manager reports values that are a little off from what the user selected, and the
// slider should not jump around after the user has released it.
bool should_update_slider(int32_t cached, int32_t value) {
  int32_t diff = abs(cached - value);
  return diff >= 3 || ((value == 0 || value == 100) && diff != 0);
}
} // namespace

// ---------------------------------------------------------------------------
// HomePage
// ---------------------------------------------------------------------------
void HomePage::show() {
  esp_log_level_set("HomePage", ConfigManager::log_level);

  std::shared_ptr<PageHandlers> handlers = InterfaceManager::begin_page(HomePage::unshow);

  std::shared_ptr<NSPanelConfig> config;
  if (NSPM_ConfigManager::get_config(&config) == ESP_OK) {
    HomePage::_last_default_room.set(config->default_room);
  } else {
    ESP_LOGW("HomePage", "Failed to get new config. Can not update HomePage local 'cur config'.");
  }

  if (HomePage::_special_mode_timer_handle == NULL) {
    // This is the first time this page is shown, create timers and the input task
    HomePage::_special_mode_create_timer_args = {};
    HomePage::_special_mode_create_timer_args.callback = &HomePage::_special_mode_timer_callback;
    HomePage::_special_mode_create_timer_args.name = "special_mode_timer";
    esp_err_t err = esp_timer_create(&HomePage::_special_mode_create_timer_args, &HomePage::_special_mode_timer_handle);
    if (err != ESP_OK) {
      ESP_LOGE("HomePage", "Failed to create 'Special mode timer'. Special mode will not be functional! Got error: %s", esp_err_to_name(err));
    }

    HomePage::_special_mode_timeout_create_timer_args = {};
    HomePage::_special_mode_timeout_create_timer_args.callback = &HomePage::_special_mode_timeout_timer_callback;
    HomePage::_special_mode_timeout_create_timer_args.name = "special_mode_timeout_timer";
    err = esp_timer_create(&HomePage::_special_mode_timeout_create_timer_args, &HomePage::_special_mode_timeout_timer_handle);
    if (err != ESP_OK) {
      ESP_LOGE("HomePage", "Failed to create 'Special mode timeout timer'. Special mode will not be functional! Got error: %s", esp_err_to_name(err));
    }

    HomePage::_input_queue = xQueueCreate(8, sizeof(InputEvent));
    if (HomePage::_input_queue == NULL || xTaskCreatePinnedToCore(HomePage::_task_handle_input, "home_input", 4096, NULL, 3, NULL, 1) != pdPASS) {
      ESP_LOGE("HomePage", "Failed to start input task. Home page will not react to touch!");
    }
  }

  {
    DisplayManager::Lock lock;
    build_screen();
    lv_screen_load(ui.screen);
  }

  HomePage::_update_display();
  handlers->add_to(RoomManager::event_loop(), ROOMMANAGER_EVENT, ESP_EVENT_ANY_ID, HomePage::_handle_roommanager_event);
  handlers->add(NSPM_CONFIGMANAGER_EVENT, nspm_configmanager_event::CONFIG_LOADED, HomePage::_handle_config_update);
}

void HomePage::unshow() {
  // Event handlers stop with the page: InterfaceManager only dispatches to the current page.
  HomePage::_current_affect_mode = HomePageAffectMode::ROOM;
  HomePage::_current_edit_mode = HomePageEditMode::ALL_LIGHTS;
}

void HomePage::set_current_affect_mode(HomePageAffectMode mode) {
  HomePage::_current_affect_mode = mode;
  HomePage::_update_display();
}

void HomePage::set_current_edit_mode(HomePageEditMode mode) {
  HomePage::_current_edit_mode = mode;
  HomePage::_update_display();
}

void HomePage::queue_input(InputEvent event) {
  if (HomePage::_input_queue != NULL && xQueueSend(HomePage::_input_queue, &event, 0) != pdTRUE) {
    ESP_LOGW("HomePage", "Input queue full, dropped touch input.");
  }
}

void HomePage::_task_handle_input(void *param) {
  InputEvent event;
  for (;;) {
    if (xQueueReceive(HomePage::_input_queue, &event, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    if (!InterfaceManager::is_current_page(HomePage::unshow)) {
      continue; // Touched just before another page was shown
    }

    switch (event.type) {
    case InputEvent::CEILING_BUTTON_PRESSED:
      HomePage::_handle_master_ceiling_lights_button(true);
      break;
    case InputEvent::CEILING_BUTTON_RELEASED:
      HomePage::_handle_master_ceiling_lights_button(false);
      break;
    case InputEvent::TABLE_BUTTON_PRESSED:
      HomePage::_handle_master_table_lights_button(true);
      break;
    case InputEvent::TABLE_BUTTON_RELEASED:
      HomePage::_handle_master_table_lights_button(false);
      break;
    case InputEvent::BRIGHTNESS_SLIDER_SET:
      HomePage::_update_brightness_slider_cache(event.value);
      HomePage::_handle_brightness_slider_event();
      break;
    case InputEvent::COLOR_TEMPERATURE_SLIDER_SET:
      HomePage::_update_color_temperature_slider_cache(event.value);
      HomePage::_handle_color_temperature_slider_event();
      break;
    case InputEvent::NEXT_ROOM:
      if (HomePage::_current_affect_mode == HomePageAffectMode::ROOM) {
        RoomManager::go_to_next_room();
      }
      break;
    case InputEvent::NEXT_MODE:
      HomePage::_go_to_next_mode();
      break;
    case InputEvent::SHOW_SCENES:
      EntitiesPage::show(HomePage::_current_affect_mode == HomePageAffectMode::ALL ? EntitiesPage::display_type_t::GLOBAL_SCENES : EntitiesPage::display_type_t::SCENES);
      break;
    case InputEvent::SHOW_ROOM_ENTITIES:
      if (HomePage::_current_affect_mode == HomePageAffectMode::ROOM) {
        EntitiesPage::show(EntitiesPage::display_type_t::ENTITIES);
      }
      break;
    }
  }
}

void HomePage::_go_to_next_mode() {
  switch (HomePage::_current_affect_mode) {
  case HomePageAffectMode::ROOM: {
    std::shared_ptr<NSPanelConfig> config;
    if (NSPM_ConfigManager::get_config(&config) == ESP_OK) [[likely]] {
      if (!config->locked_to_default_room) {
        HomePage::set_current_affect_mode(HomePageAffectMode::ALL);
      } else {
        ESP_LOGW("HomePage", "Tried to enter 'All rooms' mode but panel is not allowed to control other rooms than the default.");
      }
    } else {
      ESP_LOGE("HomePage", "Failed to get config while checking if panel is allowed to access other rooms.");
    }
    break;
  }

  case HomePageAffectMode::ALL:
    HomePage::set_current_affect_mode(HomePageAffectMode::ROOM);
    break;

  default:
    ESP_LOGE("HomePage", "Unknown affect mode when processing 'next mode button' event!");
    break;
  }
}

void HomePage::_update_display() {
  ESP_LOGD("HomePage", "Updating displayed values on HomePage.");
  std::shared_ptr<NSPanelRoomStatus> status;
  const char *mode_text;
  std::string room_text;
  if (HomePage::_current_affect_mode == HomePageAffectMode::ROOM) {
    if (RoomManager::get_home_page_status(&status) != ESP_OK) [[unlikely]] {
      ESP_LOGE("HomePage", "Failed to get status object for home page. Will abort update.");
      return;
    }
    mode_text = "Room lights";
    room_text = status->name;
  } else if (HomePage::_current_affect_mode == HomePageAffectMode::ALL) {
    mode_text = "All lights";
    room_text = "All";
    if (RoomManager::get_home_page_status_all_rooms(&status) != ESP_OK) [[unlikely]] {
      ESP_LOGW("HomePage", "All-rooms status not yet available; slider updates deferred until MQTT data arrives.");
      DisplayManager::Lock lock;
      if (ui.screen != nullptr) {
        set_label_text(ui.mode_label, mode_text);
        set_label_text(ui.room_label, room_text.c_str());
      }
      return;
    }
  } else {
    ESP_LOGE("HomePage", "Unknown HomePageAffectMode, will abort update!");
    return;
  }

  // What the page shows depends on the edit mode: in special mode the sliders follow the
  // selected kind of lights only, and the other kind of lights is shown as off.
  HomePageEditMode edit_mode = HomePage::_current_edit_mode;
  int32_t ceiling_brightness = status->ceiling_lights_dim_level;
  int32_t table_brightness = status->table_lights_dim_level;
  int32_t slider_brightness;
  int32_t slider_color_temperature;
  if (edit_mode == HomePageEditMode::CEILING_LIGHTS) {
    slider_brightness = status->ceiling_lights_dim_level;
    slider_color_temperature = status->ceiling_lights_color_temperature_value;
    table_brightness = 0;
  } else if (edit_mode == HomePageEditMode::TABLE_LIGHTS) {
    slider_brightness = status->table_lights_dim_level;
    slider_color_temperature = status->table_lights_color_temperature_value;
    ceiling_brightness = 0;
  } else {
    slider_brightness = status->average_dim_level;
    slider_color_temperature = status->average_color_temperature;
  }

  HomePage::_cache_ceiling_light_brightness = ceiling_brightness;
  HomePage::_cache_table_light_brightness = table_brightness;

  DisplayManager::Lock lock;
  if (ui.screen == nullptr) {
    return;
  }

  set_label_text(ui.mode_label, mode_text);
  set_label_text(ui.room_label, room_text.c_str());
  set_label_value(ui.ceiling_label, ceiling_brightness);
  set_label_value(ui.table_label, table_brightness);
  set_light_on(ui.ceiling_card, ui.ceiling_on, ceiling_brightness > 0);
  set_light_on(ui.table_card, ui.table_on, table_brightness > 0);

  set_highlighted(ui.ceiling_card, edit_mode == HomePageEditMode::CEILING_LIGHTS);
  set_highlighted(ui.table_card, edit_mode == HomePageEditMode::TABLE_LIGHTS);
  set_slider_line_color(ui.brightness_line, edit_mode != HomePageEditMode::ALL_LIGHTS);
  set_slider_line_color(ui.color_temperature_line, edit_mode != HomePageEditMode::ALL_LIGHTS);

  if (!ui.brightness_dragging && should_update_slider(HomePage::_cache_brightness_slider, slider_brightness)) {
    HomePage::_cache_brightness_slider = slider_brightness;
    ui.brightness_value = slider_brightness;
    set_slider_line(ui.brightness_line, slider_brightness);
  }
  if (!ui.color_temperature_dragging && should_update_slider(HomePage::_cache_color_temperature_slider, slider_color_temperature)) {
    HomePage::_cache_color_temperature_slider = slider_color_temperature;
    ui.color_temperature_value = slider_color_temperature;
    set_slider_line(ui.color_temperature_line, slider_color_temperature);
  }
}

void HomePage::_handle_roommanager_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id == roommanager_event_t::HOME_PAGE_UPDATED && HomePage::_current_affect_mode == HomePageAffectMode::ROOM) {
    HomePage::_update_display();
  } else if (event_id == roommanager_event_t::HOME_PAGE_ALL_ROOMS_UPDATED && HomePage::_current_affect_mode == HomePageAffectMode::ALL) {
    HomePage::_update_display();
  }
}

void HomePage::_update_brightness_slider_cache(int32_t value) {
  std::shared_ptr<NSPanelConfig> config;
  if (NSPM_ConfigManager::get_config(&config) == ESP_OK && value >= config->raise_light_level_to_100_above) {
    value = 100;
    // Show that the light level was raised to 100
    DisplayManager::Lock lock;
    if (ui.screen != nullptr) {
      ui.brightness_value = value;
      set_slider_line(ui.brightness_line, value);
    }
  }
  HomePage::_cache_brightness_slider = value;
}

void HomePage::_update_color_temperature_slider_cache(int32_t value) {
  HomePage::_cache_color_temperature_slider = value;
}

void HomePage::_handle_config_update(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  std::shared_ptr<NSPanelConfig> config;
  if (NSPM_ConfigManager::get_config(&config) == ESP_OK) {
    std::optional<int32_t> last_default_room = HomePage::_last_default_room.get();
    if (last_default_room.has_value() && config->default_room != *last_default_room) { // Has the default page changed
      if (RoomManager::get_current_room_id() == *last_default_room) {                  // Are we currently displaying the 'default page'? If so, go to new default page.
        ESP_LOGD("HomePage", "Default room page changed. Navigating to new default room.");
        RoomManager::go_to_room_id(config->default_room);
      }
    }
    HomePage::_last_default_room.set(config->default_room);
  } else {
    ESP_LOGW("HomePage", "Failed to get new config. Can not determine if 'default page' has changed.");
  }
}

void HomePage::_handle_master_ceiling_lights_button(bool pressed) {
  if (pressed) {
    if (HomePage::_current_edit_mode == HomePageEditMode::ALL_LIGHTS) {
      // We are currently pressing the "Ceiling lights master button", start timer for special mode. Only activate special mode if
      // we are currently not already in special mode.
      std::shared_ptr<NSPanelConfig> config;
      if (NSPM_ConfigManager::get_config(&config) == ESP_OK) {
        esp_timer_stop(HomePage::_special_mode_timer_handle); // Stop special mode timer in case it's running even though it shouldn't be.
        HomePage::_special_mode_timer_activation_mode = HomePageEditMode::CEILING_LIGHTS;
        esp_timer_start_once(HomePage::_special_mode_timer_handle, config->special_mode_trigger_time * 1000);
      } else {
        ESP_LOGE("HomePage", "Failed to get NSPanel Config from NSPM_ConfigManager!");
      }
    } else {
      // We pressed the button and we are currently in a special mode, exit special mode.
      if (esp_timer_is_active(HomePage::_special_mode_timer_handle) == 1) {
        esp_timer_stop(HomePage::_special_mode_timer_handle); // Stop special mode timer in case it's running even though it shouldn't be.
      }
      HomePage::_special_mode_timer_activation_mode = HomePageEditMode::ALL_LIGHTS;
      HomePage::_special_mode_timeout_timer_callback(NULL); // Revert to "normal mode"

      // This button press canceled the "special mode", ignore touch up event that follows this touch down event.
      HomePage::_ignore_next_ceiling_button_touch_up = true;
    }
  } else {
    // Should we ignore this event?
    if (HomePage::_ignore_next_ceiling_button_touch_up) {
      HomePage::_ignore_next_ceiling_button_touch_up = false;
      return;
    }
    // We are in "normal mode", proceed as normal
    if (HomePage::_current_edit_mode == HomePageEditMode::ALL_LIGHTS) {
      // We released the button and are currently not in special mode, therefore the timer
      // hasn't trigger yet so stop it ans the user hasn't held the button long enough
      // for special mode to trigger. This was a normal press of the button.
      if (esp_timer_is_active(HomePage::_special_mode_timer_handle) == 1) {
        esp_timer_stop(HomePage::_special_mode_timer_handle); // Stop special mode timer in case it's running even though it shouldn't be.
        HomePage::_special_mode_timer_activation_mode = HomePageEditMode::ALL_LIGHTS;
      }

      HomePage::_send_ceiling_master_button_command_to_manager();
    } else if (HomePage::_current_edit_mode == HomePage::TABLE_LIGHTS) {
      // We pressed the button and we are currently in a special mode, exit special mode.
      if (esp_timer_is_active(HomePage::_special_mode_timer_handle) == 1) {
        esp_timer_stop(HomePage::_special_mode_timer_handle); // Stop special mode timer in case it's running even though it shouldn't be.
      }
      HomePage::_special_mode_timer_activation_mode = HomePageEditMode::ALL_LIGHTS;
      HomePage::_special_mode_timeout_timer_callback(NULL); // Revert to "normal mode"
    }
  }
}

void HomePage::_handle_master_table_lights_button(bool pressed) {
  if (pressed) {
    if (HomePage::_current_edit_mode == HomePageEditMode::ALL_LIGHTS) {
      // We are currently pressing the "Table lights master button", start timer for special mode. Only activate special mode if
      // we are currently not already in special mode.
      std::shared_ptr<NSPanelConfig> config;
      if (NSPM_ConfigManager::get_config(&config) == ESP_OK) {
        esp_timer_stop(HomePage::_special_mode_timer_handle); // Stop special mode timer in case it's running even though it shouldn't be.
        HomePage::_special_mode_timer_activation_mode = HomePageEditMode::TABLE_LIGHTS;
        esp_timer_start_once(HomePage::_special_mode_timer_handle, config->special_mode_trigger_time * 1000);
      } else {
        ESP_LOGE("HomePage", "Failed to get NSPanel Config from NSPM_ConfigManager!");
      }
    } else {
      // We pressed the button and we are currently in a special mode, exit special mode.
      if (esp_timer_is_active(HomePage::_special_mode_timer_handle) == 1) {
        esp_timer_stop(HomePage::_special_mode_timer_handle); // Stop special mode timer in case it's running even though it shouldn't be.
      }
      HomePage::_special_mode_timer_activation_mode = HomePageEditMode::ALL_LIGHTS;
      HomePage::_special_mode_timeout_timer_callback(NULL); // Revert to "normal mode"

      // This button press canceled the "special mode", ignore touch up event that follows this touch down event.
      HomePage::_ignore_next_table_button_touch_up = true;
    }
  } else {
    // Should we ignore this event?
    if (HomePage::_ignore_next_table_button_touch_up) {
      HomePage::_ignore_next_table_button_touch_up = false;
      return;
    }
    // We are in "normal mode", proceed as normal
    if (HomePage::_current_edit_mode == HomePageEditMode::ALL_LIGHTS) {
      // We released the button and are currently not in special mode, therefore the timer
      // hasn't trigger yet so stop it ans the user hasn't held the button long enough
      // for special mode to trigger. This was a normal press of the button.
      if (esp_timer_is_active(HomePage::_special_mode_timer_handle) == 1) {
        esp_timer_stop(HomePage::_special_mode_timer_handle); // Stop special mode timer in case it's running even though it shouldn't be.
        HomePage::_special_mode_timer_activation_mode = HomePageEditMode::ALL_LIGHTS;
      }

      HomePage::_send_table_master_button_command_to_manager();
    } else if (HomePage::_current_edit_mode == HomePage::CEILING_LIGHTS) {
      // We pressed the button and we are currently in a special mode, exit special mode.
      if (esp_timer_is_active(HomePage::_special_mode_timer_handle) == 1) {
        esp_timer_stop(HomePage::_special_mode_timer_handle); // Stop special mode timer in case it's running even though it shouldn't be.
      }
      HomePage::_special_mode_timer_activation_mode = HomePageEditMode::ALL_LIGHTS;
      HomePage::_special_mode_timeout_timer_callback(NULL); // Revert to "normal mode"
    }
  }
}

void HomePage::_send_ceiling_master_button_command_to_manager() {
  std::shared_ptr<NSPanelConfig> config;
  if (NSPM_ConfigManager::get_config(&config) != ESP_OK) [[unlikely]] {
    ESP_LOGE("HomePage", "Failed to get config, cannot proceed with sending command to manager.");
    return;
  }

  ESP_LOGD("HomePage", "Send ceiling master button event.");
  if (HomePage::_current_affect_mode == HomePageAffectMode::ROOM) {
    std::shared_ptr<NSPanelRoomStatus> status;
    if (RoomManager::get_home_page_status(&status) == ESP_OK) {
      if (status->num_ceiling_lights > 0) {
        NSPanelMQTTManagerCommand__FirstPageTurnLightOn turn_light_on_cmd = NSPANEL_MQTTMANAGER_COMMAND__FIRST_PAGE_TURN_LIGHT_ON__INIT;
        turn_light_on_cmd.has_brightness_value = true;
        if (status->ceiling_lights_dim_level > 0) {
          turn_light_on_cmd.brightness_slider_value = 0;
        } else {
          turn_light_on_cmd.brightness_slider_value = HomePage::_cache_brightness_slider;
          if (turn_light_on_cmd.brightness_slider_value == 0) [[unlikely]] {
            turn_light_on_cmd.brightness_slider_value = config->default_light_brightess;
          }
        }
        turn_light_on_cmd.affect_lights = NSPANEL_MQTTMANAGER_COMMAND__AFFECT_LIGHTS_OPTIONS__CEILING_LIGHTS;
        turn_light_on_cmd.has_kelvin_value = true;
        turn_light_on_cmd.kelvin_slider_value = HomePage::_cache_color_temperature_slider;
        turn_light_on_cmd.global = false;
        turn_light_on_cmd.selected_room = status->id;

        NSPanelMQTTManagerCommand cmd = NSPANEL_MQTTMANAGER_COMMAND__INIT;
        cmd.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_FIRST_PAGE_TURN_ON;
        cmd.first_page_turn_on = &turn_light_on_cmd;
        cmd.nspanel_id = config->nspanel_id;

        uint32_t packed_length = nspanel_mqttmanager_command__get_packed_size(&cmd);
        std::vector<uint8_t> buffer(packed_length); // Use vector for automatic cleanup of data when going out of scope
        size_t packed_data_size = nspanel_mqttmanager_command__pack(&cmd, buffer.data());
        if (packed_data_size == packed_length) [[likely]] {
          if (MqttManager::publish(NSPM_ConfigManager::get_manager_command_topic(), (const char *)buffer.data(), packed_length, false) == ESP_OK) [[likely]] {
            std::shared_ptr<NSPanelConfig> config;
            if (NSPM_ConfigManager::get_config(&config) == ESP_OK) [[likely]] {
              std::shared_ptr<NSPanelRoomStatus> mut_status;
              if (RoomManager::get_home_page_status_mutable(&mut_status) == ESP_OK) [[likely]] {
                // Does room contain ceiling lights?
                // If all lights were off, they are now all on
                if (mut_status->ceiling_lights_dim_level == 0) {
                  mut_status->ceiling_lights_dim_level = HomePage::_cache_brightness_slider;
                  mut_status->num_ceiling_lights_on = mut_status->num_ceiling_lights;
                } else {
                  mut_status->ceiling_lights_dim_level = 0;
                  mut_status->num_ceiling_lights_on = 0;
                }
                // Calculate new average of total color temperature:
                uint16_t total_light_level = mut_status->ceiling_lights_dim_level * mut_status->num_ceiling_lights_on;
                uint16_t number_of_lights_on = mut_status->num_ceiling_lights_on;
                total_light_level += mut_status->table_lights_dim_level * mut_status->num_table_lights_on;
                number_of_lights_on += mut_status->num_table_lights_on;

                if (number_of_lights_on > 0) {
                  mut_status->average_dim_level = total_light_level / number_of_lights_on;
                }
                RoomManager::replace_home_page_status(mut_status);
              } else {
                ESP_LOGW("HomePage", "Failed to get config when checking if optimistic mode is active. Will now update optimistically.");
              }
            } else {
              ESP_LOGW("HomePage", "Failed to get config when checking if optimistic mode is active. Will now update optimistically.");
            }
          } else {
            ESP_LOGE("HomePage", "Failed to publish data while sending command to manager from 'ceiling master button'!");
          }
        } else {
          ESP_LOGE("HomePage", "Failed to pack data while sending command to manager from 'ceiling master button'!");
        }
      }
    } else {
      ESP_LOGE("HomePage", "Failed to get current page room status when trying to send 'ceiling master button' command to manager!");
    }
  } else if (HomePage::_current_affect_mode == HomePageAffectMode::ALL) {
    std::shared_ptr<NSPanelRoomStatus> status;
    if (RoomManager::get_home_page_status_all_rooms(&status) == ESP_OK) [[likely]] {
      NSPanelMQTTManagerCommand__FirstPageTurnLightOn turn_light_on_cmd = NSPANEL_MQTTMANAGER_COMMAND__FIRST_PAGE_TURN_LIGHT_ON__INIT;
      turn_light_on_cmd.has_brightness_value = true;
      if (status->ceiling_lights_dim_level > 0) {
        turn_light_on_cmd.brightness_slider_value = 0;
      } else {
        turn_light_on_cmd.brightness_slider_value = HomePage::_cache_brightness_slider;
        if (turn_light_on_cmd.brightness_slider_value == 0) [[unlikely]] {
          turn_light_on_cmd.brightness_slider_value = config->default_light_brightess;
        }
      }
      turn_light_on_cmd.affect_lights = NSPANEL_MQTTMANAGER_COMMAND__AFFECT_LIGHTS_OPTIONS__CEILING_LIGHTS;
      turn_light_on_cmd.has_kelvin_value = true;
      turn_light_on_cmd.kelvin_slider_value = HomePage::_cache_color_temperature_slider;
      turn_light_on_cmd.global = true;     // Affect all rooms
      turn_light_on_cmd.selected_room = 0; // Set to 0 as it doesn't matter when "global" is set to true

      NSPanelMQTTManagerCommand cmd = NSPANEL_MQTTMANAGER_COMMAND__INIT;
      cmd.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_FIRST_PAGE_TURN_ON;
      cmd.first_page_turn_on = &turn_light_on_cmd;
      cmd.nspanel_id = config->nspanel_id;

      uint32_t packed_length = nspanel_mqttmanager_command__get_packed_size(&cmd);
      std::vector<uint8_t> buffer(packed_length); // Use vector for automatic cleanup of data when going out of scope
      size_t packed_data_size = nspanel_mqttmanager_command__pack(&cmd, buffer.data());
      if (packed_data_size == packed_length) [[likely]] {
        if (MqttManager::publish(NSPM_ConfigManager::get_manager_command_topic(), (const char *)buffer.data(), packed_length, false) == ESP_OK) [[likely]] {
          std::shared_ptr<NSPanelConfig> config;
          if (NSPM_ConfigManager::get_config(&config) == ESP_OK) [[likely]] {
            if (config->optimistic_mode) [[likely]] {
              std::shared_ptr<NSPanelRoomStatus> mutable_status;

              // We are in optimistic mode, update all rooms with new values
              if (RoomManager::get_home_page_status_mutable_all_rooms(&mutable_status) == ESP_OK) [[likely]] {
                if (status->average_dim_level == 0) {
                  mutable_status->ceiling_lights_dim_level = HomePage::_cache_brightness_slider;
                  mutable_status->num_ceiling_lights_on = mutable_status->num_ceiling_lights;
                } else {
                  mutable_status->ceiling_lights_dim_level = 0;
                  mutable_status->num_ceiling_lights_on = 0;
                }
                RoomManager::replace_home_page_status_all_rooms(mutable_status);
              } else {
                ESP_LOGW("HomePage", "Failed to get mutable room status. Will wait for return result instead.");
              }
            }
          }
        } else {
          ESP_LOGW("HomePage", "Failed to get NSPM config when checking if panel is optimistic. Will wait for return result instead.");
        }
      } else {
        ESP_LOGE("HomePage", "Failed to publish data while sending command to manager from 'ceiling master button'!");
      }
    } else {
      ESP_LOGE("HomePage", "Failed to get home page status for all rooms. Will abort!");
    }
  } else {
    ESP_LOGE("HomePage", "Failed to pack data while sending command to manager from 'ceiling master button'!");
  }
}

void HomePage::_send_table_master_button_command_to_manager() {
  std::shared_ptr<NSPanelConfig> config;
  if (NSPM_ConfigManager::get_config(&config) != ESP_OK) [[unlikely]] {
    ESP_LOGE("HomePage", "Failed to get config, cannot proceed with sending command to manager.");
    return;
  }

  ESP_LOGD("HomePage", "Send table master button event.");
  if (HomePage::_current_affect_mode == HomePageAffectMode::ROOM) {
    std::shared_ptr<NSPanelRoomStatus> status;
    if (RoomManager::get_home_page_status(&status) == ESP_OK) {
      if (status->num_table_lights > 0) {
        // Build and send command to turn on/off table lights
        NSPanelMQTTManagerCommand__FirstPageTurnLightOn turn_light_on_cmd = NSPANEL_MQTTMANAGER_COMMAND__FIRST_PAGE_TURN_LIGHT_ON__INIT;
        turn_light_on_cmd.has_brightness_value = true;
        if (status->table_lights_dim_level > 0) {
          turn_light_on_cmd.brightness_slider_value = 0;
        } else {
          turn_light_on_cmd.brightness_slider_value = HomePage::_cache_brightness_slider;
          if (turn_light_on_cmd.brightness_slider_value == 0) [[unlikely]] {
            turn_light_on_cmd.brightness_slider_value = config->default_light_brightess;
          }
        }
        turn_light_on_cmd.affect_lights = NSPANEL_MQTTMANAGER_COMMAND__AFFECT_LIGHTS_OPTIONS__TABLE_LIGHTS;
        turn_light_on_cmd.has_kelvin_value = true;
        turn_light_on_cmd.kelvin_slider_value = HomePage::_cache_color_temperature_slider;
        turn_light_on_cmd.global = false;
        turn_light_on_cmd.selected_room = status->id;

        NSPanelMQTTManagerCommand cmd = NSPANEL_MQTTMANAGER_COMMAND__INIT;
        cmd.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_FIRST_PAGE_TURN_ON;
        cmd.first_page_turn_on = &turn_light_on_cmd;
        cmd.nspanel_id = config->nspanel_id;

        uint32_t packed_length = nspanel_mqttmanager_command__get_packed_size(&cmd);
        std::vector<uint8_t> buffer(packed_length); // Use vector for automatic cleanup of data when going out of scope
        size_t packed_data_size = nspanel_mqttmanager_command__pack(&cmd, buffer.data());
        if (packed_data_size == packed_length) [[likely]] {
          if (MqttManager::publish(NSPM_ConfigManager::get_manager_command_topic(), (const char *)buffer.data(), packed_length, false) == ESP_OK) [[likely]] {
            std::shared_ptr<NSPanelConfig> config;
            if (NSPM_ConfigManager::get_config(&config) == ESP_OK) [[likely]] {
              std::shared_ptr<NSPanelRoomStatus> mut_status;
              if (RoomManager::get_home_page_status_mutable(&mut_status) == ESP_OK) [[likely]] {
                // Does room contain table lights?
                // If all lights were off, they are now all on
                if (mut_status->table_lights_dim_level == 0) {
                  mut_status->table_lights_dim_level = HomePage::_cache_brightness_slider;
                  mut_status->num_table_lights_on = mut_status->num_table_lights;
                } else {
                  mut_status->table_lights_dim_level = 0;
                  mut_status->num_table_lights_on = 0;
                }
                // Calculate new average of total color temperature:
                uint16_t total_light_level = mut_status->ceiling_lights_dim_level * mut_status->num_ceiling_lights_on;
                uint16_t number_of_lights_on = mut_status->num_ceiling_lights_on;
                total_light_level += mut_status->table_lights_dim_level * mut_status->num_table_lights_on;
                number_of_lights_on += mut_status->num_table_lights_on;

                if (number_of_lights_on > 0) {
                  mut_status->average_dim_level = total_light_level / number_of_lights_on;
                }
                RoomManager::replace_home_page_status(mut_status);
              } else {
                ESP_LOGW("HomePage", "Failed to get config when checking if optimistic mode is active. Will now update optimistically.");
              }
            } else {
              ESP_LOGW("HomePage", "Failed to get config when checking if optimistic mode is active. Will now update optimistically.");
            }
          } else {
            ESP_LOGE("HomePage", "Failed to publish data while sending command to manager from 'table master button'!");
          }
        } else {
          ESP_LOGE("HomePage", "Failed to pack data while sending command to manager from 'table master button'!");
        }
      }
    } else {
      ESP_LOGE("HomePage", "Failed to get current page room status when trying to send 'table master button' command to manager!");
    }
  } else if (HomePage::_current_affect_mode == HomePageAffectMode::ALL) {
    std::shared_ptr<NSPanelRoomStatus> status;
    if (RoomManager::get_home_page_status_all_rooms(&status) == ESP_OK) [[likely]] {
      NSPanelMQTTManagerCommand__FirstPageTurnLightOn turn_light_on_cmd = NSPANEL_MQTTMANAGER_COMMAND__FIRST_PAGE_TURN_LIGHT_ON__INIT;
      turn_light_on_cmd.has_brightness_value = true;
      if (status->table_lights_dim_level > 0) {
        turn_light_on_cmd.brightness_slider_value = 0;
      } else {
        turn_light_on_cmd.brightness_slider_value = HomePage::_cache_brightness_slider;
        if (turn_light_on_cmd.brightness_slider_value == 0) [[unlikely]] {
          turn_light_on_cmd.brightness_slider_value = config->default_light_brightess;
        }
      }
      turn_light_on_cmd.affect_lights = NSPANEL_MQTTMANAGER_COMMAND__AFFECT_LIGHTS_OPTIONS__TABLE_LIGHTS;
      turn_light_on_cmd.has_kelvin_value = true;
      turn_light_on_cmd.kelvin_slider_value = HomePage::_cache_color_temperature_slider;
      turn_light_on_cmd.global = true;     // Affect all rooms
      turn_light_on_cmd.selected_room = 0; // Set to 0 as it doesn't matter when "global" is set to true

      NSPanelMQTTManagerCommand cmd = NSPANEL_MQTTMANAGER_COMMAND__INIT;
      cmd.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_FIRST_PAGE_TURN_ON;
      cmd.first_page_turn_on = &turn_light_on_cmd;
      cmd.nspanel_id = config->nspanel_id;

      uint32_t packed_length = nspanel_mqttmanager_command__get_packed_size(&cmd);
      std::vector<uint8_t> buffer(packed_length); // Use vector for automatic cleanup of data when going out of scope
      size_t packed_data_size = nspanel_mqttmanager_command__pack(&cmd, buffer.data());
      if (packed_data_size == packed_length) [[likely]] {
        if (MqttManager::publish(NSPM_ConfigManager::get_manager_command_topic(), (const char *)buffer.data(), packed_length, false) == ESP_OK) [[likely]] {
          std::shared_ptr<NSPanelConfig> config;
          if (NSPM_ConfigManager::get_config(&config) == ESP_OK) [[likely]] {
            if (config->optimistic_mode) [[likely]] {
              std::shared_ptr<NSPanelRoomStatus> mutable_status;

              // We are in optimistic mode, update all rooms with new values
              if (RoomManager::get_home_page_status_mutable_all_rooms(&mutable_status) == ESP_OK) [[likely]] {
                if (status->average_dim_level == 0) {
                  mutable_status->table_lights_dim_level = HomePage::_cache_brightness_slider;
                  mutable_status->num_table_lights_on = mutable_status->num_table_lights;
                } else {
                  mutable_status->table_lights_dim_level = 0;
                  mutable_status->num_table_lights_on = 0;
                }
                RoomManager::replace_home_page_status_all_rooms(mutable_status);
              } else {
                ESP_LOGW("HomePage", "Failed to get mutable room status. Will wait for return result instead.");
              }
            }
          }
        } else {
          ESP_LOGW("HomePage", "Failed to get NSPM config when checking if panel is optimistic. Will wait for return result instead.");
        }
      } else {
        ESP_LOGE("HomePage", "Failed to publish data while sending command to manager from 'ceiling master button'!");
      }
    } else {
      ESP_LOGE("HomePage", "Failed to get home page status for all rooms. Will abort!");
    }
  }
}

void HomePage::_special_mode_timer_callback(void *arg) {
  ESP_LOGI("HomePage", "Special mode timer triggered!");
  HomePage::_current_edit_mode = HomePage::_special_mode_timer_activation_mode;

  if (HomePage::_current_edit_mode == HomePageEditMode::CEILING_LIGHTS) {
    HomePage::_ignore_next_ceiling_button_touch_up = true;
    HomePage::_ignore_next_table_button_touch_up = false;
  } else if (HomePage::_current_edit_mode == HomePageEditMode::TABLE_LIGHTS) {
    HomePage::_ignore_next_ceiling_button_touch_up = false;
    HomePage::_ignore_next_table_button_touch_up = true;
  } else {
    ESP_LOGE("HomePage", "Unknown EditMode when processing special mode timer callback.");
  }

  std::shared_ptr<NSPanelConfig> config;
  if (NSPM_ConfigManager::get_config(&config) == ESP_OK) {
    esp_timer_stop(HomePage::_special_mode_timeout_timer_handle);
    esp_err_t result = esp_timer_start_once(HomePage::_special_mode_timeout_timer_handle, config->special_mode_release_time * 1000);
    if (result != ESP_OK) {
      ESP_LOGE("HomePage", "Error while starting timer to exit special mode! Got error: %s", esp_err_to_name(result));
    }
  } else {
    ESP_LOGW("HomePage", "Failed to get NSPanel Config from NSPM_ConfigManager. Will set default special mode timeout of 5 seconds.");
    esp_timer_stop(HomePage::_special_mode_timeout_timer_handle);
    esp_err_t result = esp_timer_start_once(HomePage::_special_mode_timeout_timer_handle, 5000 * 1000);
    if (result != ESP_OK) {
      ESP_LOGE("HomePage", "Error while restarting timer to exit special mode! Got error: %s", esp_err_to_name(result));
    }
  }

  HomePage::_update_display();
}

void HomePage::_special_mode_timeout_timer_callback(void *arg) {
  if (HomePage::_current_edit_mode != HomePageEditMode::ALL_LIGHTS) {
    // Reset to "normal mode"
    HomePage::_current_edit_mode = HomePageEditMode::ALL_LIGHTS; // Reset activation mode
    HomePage::_ignore_next_ceiling_button_touch_up = false;
    HomePage::_ignore_next_table_button_touch_up = false;
    HomePage::_update_display();
  }
}

void HomePage::_handle_brightness_slider_event() {
  std::shared_ptr<NSPanelRoomStatus> status;
  if (RoomManager::get_home_page_status(&status) != ESP_OK) {
    ESP_LOGE("HomePage", "Failed to get home page status when trying to update brightness slider event.");
    return;
  }

  NSPanelMQTTManagerCommand__FirstPageTurnLightOn turn_light_on_cmd = NSPANEL_MQTTMANAGER_COMMAND__FIRST_PAGE_TURN_LIGHT_ON__INIT;
  turn_light_on_cmd.has_brightness_value = true;
  turn_light_on_cmd.brightness_slider_value = HomePage::_cache_brightness_slider;
  turn_light_on_cmd.has_kelvin_value = false;
  turn_light_on_cmd.kelvin_slider_value = 0;
  turn_light_on_cmd.selected_room = status->id;

  ESP_LOGD("HomePage", "Sending command to request brightness %ld%%", turn_light_on_cmd.brightness_slider_value);

  if (HomePage::_current_edit_mode == HomePageEditMode::ALL_LIGHTS) {
    turn_light_on_cmd.affect_lights = NSPANEL_MQTTMANAGER_COMMAND__AFFECT_LIGHTS_OPTIONS__ALL;
  } else if (HomePage::_current_edit_mode == HomePageEditMode::CEILING_LIGHTS) {
    turn_light_on_cmd.affect_lights = NSPANEL_MQTTMANAGER_COMMAND__AFFECT_LIGHTS_OPTIONS__CEILING_LIGHTS;
  } else if (HomePage::_current_edit_mode == HomePageEditMode::TABLE_LIGHTS) {
    turn_light_on_cmd.affect_lights = NSPANEL_MQTTMANAGER_COMMAND__AFFECT_LIGHTS_OPTIONS__TABLE_LIGHTS;
  } else {
    ESP_LOGE("HomePage", "Unknown edit mode when sending command from slider event, will cancel.");
    return;
  }

  if (HomePage::_current_affect_mode == HomePageAffectMode::ROOM) {
    turn_light_on_cmd.global = false;
  } else if (HomePage::_current_affect_mode == HomePageAffectMode::ALL) {
    turn_light_on_cmd.global = true;
  } else {
    ESP_LOGE("HomePage", "Unknown affect mode when sending command from slider event, will cancel.");
    return;
  }

  NSPanelMQTTManagerCommand cmd = NSPANEL_MQTTMANAGER_COMMAND__INIT;
  cmd.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_FIRST_PAGE_TURN_ON;
  cmd.first_page_turn_on = &turn_light_on_cmd;
  cmd.nspanel_id = NSPM_ConfigManager::get_nspanel_id();

  uint32_t packed_length = nspanel_mqttmanager_command__get_packed_size(&cmd);
  std::vector<uint8_t> buffer(packed_length); // Use vector for automatic cleanup of data when going out of scope
  size_t packed_data_size = nspanel_mqttmanager_command__pack(&cmd, buffer.data());

  if (packed_data_size == packed_length) [[likely]] {
    if (MqttManager::publish(NSPM_ConfigManager::get_manager_command_topic(), (const char *)buffer.data(), packed_length, false) == ESP_OK) [[likely]] {
      std::shared_ptr<NSPanelConfig> config;
      if (NSPM_ConfigManager::get_config(&config) == ESP_OK) [[likely]] {
        // Get the correct mutable status object.
        std::shared_ptr<NSPanelRoomStatus> mut_status;
        if (HomePage::_current_affect_mode == HomePageAffectMode::ROOM) {
          if (RoomManager::get_home_page_status_mutable(&mut_status) == ESP_OK) [[likely]] {
          } else {
            ESP_LOGE("HomePage", "Failed to get mutable home page status. Will abort.");
            return;
          }
        } else if (HomePage::_current_affect_mode == HomePageAffectMode::ALL) {
          if (RoomManager::get_home_page_status_mutable_all_rooms(&mut_status) == ESP_OK) [[likely]] {
          } else {
            ESP_LOGE("HomePage", "Failed to get mutable home page status. Will abort.");
            return;
          }
        } else {
          ESP_LOGW("HomePage", "Failed to get config when checking if optimistic mode is active. Will not update optimistically.");
          return;
        }

        if (mut_status->ceiling_lights_dim_level == 0 && mut_status->table_lights_dim_level == 0) {
          mut_status->ceiling_lights_dim_level = HomePage::_cache_brightness_slider;
          mut_status->num_ceiling_lights_on = mut_status->num_ceiling_lights;
          mut_status->table_lights_dim_level = HomePage::_cache_brightness_slider;
          mut_status->num_table_lights_on = mut_status->num_table_lights;
        } else if (mut_status->ceiling_lights_dim_level > 0 && mut_status->table_lights_dim_level == 0) {
          mut_status->ceiling_lights_dim_level = HomePage::_cache_brightness_slider;
          mut_status->num_ceiling_lights_on = mut_status->num_ceiling_lights;
        } else if (mut_status->ceiling_lights_dim_level == 0 && mut_status->table_lights_dim_level > 0) {
          mut_status->table_lights_dim_level = HomePage::_cache_brightness_slider;
          mut_status->num_table_lights_on = mut_status->num_table_lights;
        }
        // Calculate new average of total color temperature:
        uint16_t total_light_level = mut_status->ceiling_lights_dim_level * mut_status->num_ceiling_lights_on;
        uint16_t number_of_lights_on = mut_status->num_ceiling_lights_on;
        total_light_level += mut_status->table_lights_dim_level * mut_status->num_table_lights_on;
        number_of_lights_on += mut_status->num_table_lights_on;

        if (number_of_lights_on > 0) {
          mut_status->average_dim_level = total_light_level / number_of_lights_on;
        }
        if (HomePage::_current_affect_mode == HomePageAffectMode::ROOM) {
          if (RoomManager::replace_home_page_status(mut_status) != ESP_OK) [[unlikely]] {
            ESP_LOGE("HomePage", "Failed to get mutable home page status. Will abort.");
            return;
          }
        } else if (HomePage::_current_affect_mode == HomePageAffectMode::ALL) {
          if (RoomManager::replace_home_page_status_all_rooms(mut_status) != ESP_OK) [[unlikely]] {
            ESP_LOGE("HomePage", "Failed to get mutable home page status. Will abort.");
            return;
          }
        } else {
          ESP_LOGW("HomePage", "Failed to get config when checking if optimistic mode is active. Will not update optimistically.");
          return;
        }

      } else {
        ESP_LOGW("HomePage", "Failed to get config when checking if optimistic mode is active. Will not update optimistically.");
      }
    } else {
      ESP_LOGE("HomePage", "Failed to publish data while sending command to manager from 'brightness slider event'!");
    }
  } else {
    ESP_LOGE("HomePage", "Failed to pack data while sending command to manager from 'brightness slider event'!");
  }
}

void HomePage::_handle_color_temperature_slider_event() {
  std::shared_ptr<NSPanelRoomStatus> status;
  if (RoomManager::get_home_page_status(&status) != ESP_OK) {
    ESP_LOGE("HomePage", "Failed to get home page status when trying to update color temperature slider event.");
    return;
  }

  NSPanelMQTTManagerCommand__FirstPageTurnLightOn turn_light_on_cmd = NSPANEL_MQTTMANAGER_COMMAND__FIRST_PAGE_TURN_LIGHT_ON__INIT;
  turn_light_on_cmd.has_brightness_value = false;
  turn_light_on_cmd.brightness_slider_value = 0;
  turn_light_on_cmd.has_kelvin_value = true;
  turn_light_on_cmd.kelvin_slider_value = HomePage::_cache_color_temperature_slider;
  turn_light_on_cmd.selected_room = status->id;

  ESP_LOGD("HomePage", "Sending command to request color temp %ld%%", turn_light_on_cmd.kelvin_slider_value);

  if (HomePage::_current_edit_mode == HomePageEditMode::ALL_LIGHTS) {
    turn_light_on_cmd.affect_lights = NSPANEL_MQTTMANAGER_COMMAND__AFFECT_LIGHTS_OPTIONS__ALL;
  } else if (HomePage::_current_edit_mode == HomePageEditMode::CEILING_LIGHTS) {
    turn_light_on_cmd.affect_lights = NSPANEL_MQTTMANAGER_COMMAND__AFFECT_LIGHTS_OPTIONS__CEILING_LIGHTS;
  } else if (HomePage::_current_edit_mode == HomePageEditMode::TABLE_LIGHTS) {
    turn_light_on_cmd.affect_lights = NSPANEL_MQTTMANAGER_COMMAND__AFFECT_LIGHTS_OPTIONS__TABLE_LIGHTS;
  } else {
    ESP_LOGE("HomePage", "Unknown edit mode when sending command from slider event, will cancel.");
    return;
  }

  if (HomePage::_current_affect_mode == HomePageAffectMode::ROOM) {
    turn_light_on_cmd.global = false;
  } else if (HomePage::_current_affect_mode == HomePageAffectMode::ALL) {
    turn_light_on_cmd.global = true;
  } else {
    ESP_LOGE("HomePage", "Unknown affect mode when sending command from slider event, will cancel.");
    return;
  }

  NSPanelMQTTManagerCommand cmd = NSPANEL_MQTTMANAGER_COMMAND__INIT;
  cmd.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_FIRST_PAGE_TURN_ON;
  cmd.first_page_turn_on = &turn_light_on_cmd;
  cmd.nspanel_id = NSPM_ConfigManager::get_nspanel_id();

  uint32_t packed_length = nspanel_mqttmanager_command__get_packed_size(&cmd);
  std::vector<uint8_t> buffer(packed_length); // Use vector for automatic cleanup of data when going out of scope
  size_t packed_data_size = nspanel_mqttmanager_command__pack(&cmd, buffer.data());
  if (packed_data_size == packed_length) {
    if (MqttManager::publish(NSPM_ConfigManager::get_manager_command_topic(), (const char *)buffer.data(), packed_length, false) == ESP_OK) {
      // TODO: Handle optimistic mode
      // NSPanelConfig config;
      // if (NSPM_ConfigManager::get_config(&config) == ESP_OK) {
      //   if (config.optimistic_mode) {
      //     if (status.ceiling_lights_dim_level == 0 && status.table_lights_dim_level == 0) {
      //       status.average_dim_level = HomePage::_cache_brightness_slider;
      //       status.ceiling_lights_dim_level = HomePage::_cache_brightness_slider;
      //       HomePage::_update_display();
      //     }
      //     // TODO: Update total average light with weighted values when average light is not 0
      //   }
      // } else {
      //   ESP_LOGW("HomePage", "Failed to get NSPM config when checking if panel is optimistic. Will wait for return result instead.");
      // }
    } else {
      ESP_LOGE("HomePage", "Failed to publish data while sending command to manager from 'color temperature slider'!");
    }
  } else {
    ESP_LOGE("HomePage", "Failed to pack data while sending command to manager from 'color temperature slider'!");
  }
}