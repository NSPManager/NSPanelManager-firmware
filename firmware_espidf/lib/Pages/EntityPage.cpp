#include <BackgroundManager.hpp>
#include <ConfigManager.hpp>
#include <DisplayManager.hpp>
#include <EntitiesPage.hpp>
#include <EntityPage.hpp>
#include <IconPackManager.hpp>
#include <InterfaceManager.hpp>
#include <MqttManager.hpp>
#include <NSPM_ConfigManager.hpp>
#include <StatusUpdateManager.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <esp_log.h>
#include <lvgl.h>
#include <protobuf_nspanel_entity.pb-c.h>
#include <vector>

LV_FONT_DECLARE(font_screensaver_text_18);
LV_FONT_DECLARE(font_screensaver_text_15);
LV_FONT_DECLARE(font_thermostat_icons_30);
LV_FONT_DECLARE(font_entity_page_controls_40);
LV_FONT_DECLARE(font_thermostat_temperature_84);
LV_FONT_DECLARE(font_thermostat_temperature_30);

// ---------------------------------------------------------------------------
// LVGL user interface. Every function in here must be called with the LVGL
// lock held (the event callbacks run on the LVGL task, which holds it).
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t COLOR_BACKGROUND = 0xFFFFFF;
constexpr uint32_t COLOR_PANEL = 0x000000;
constexpr lv_opa_t PANEL_OPA = LV_OPA_60;
constexpr uint32_t COLOR_FOREGROUND = 0xFFFFFF;
constexpr uint32_t COLOR_HIGHLIGHT = 0xFFC000;
constexpr uint32_t COLOR_WARM = 0xFFC000;
constexpr uint32_t COLOR_COLD = 0x8FD3F5;

// font_entity_page_controls_40 holds these icons
constexpr const char *ICON_CHEVRON_UP = "U";
constexpr const char *ICON_CHEVRON_DOWN = "D";
constexpr const char *ICON_CHEVRON_LEFT = "L";
constexpr const char *ICON_CHEVRON_RIGHT = "R";
constexpr const char *ICON_OPEN = "u";
constexpr const char *ICON_CLOSE = "d";
constexpr const char *ICON_STOP = "S";
constexpr const char *ICON_TILT_OPEN = "o";
constexpr const char *ICON_TILT_CLOSE = "c";
constexpr const char *ICON_POSITION_MODE = "p";
constexpr const char *ICON_TILT_MODE = "t";

// Layout, in screen pixels
constexpr int32_t MARGIN = 8;
constexpr int32_t RADIUS = 8;
constexpr int32_t HEADER_Y = 6;
constexpr int32_t HEADER_H = 41;
constexpr int32_t HEADER_W = DisplayManager::LANDSCAPE_WIDTH - 2 * MARGIN;
constexpr int32_t HEADER_BUTTON_W = 60; // Back button on the left, mode button on the right
constexpr int32_t CONTENT_Y = HEADER_Y + HEADER_H + 7;
constexpr int32_t CONTENT_H = DisplayManager::LANDSCAPE_HEIGHT - CONTENT_Y - 6;
constexpr int32_t GAP = 6;
constexpr int32_t COLUMN_W = (HEADER_W - 2 * GAP) / 3;
constexpr int32_t SLIDER_PAD = 6; // Space between the card edge and the slider line at its ends
constexpr int32_t SLIDER_LINE_H = 3;
constexpr int32_t THERMOSTAT_OPTIONS = 5;
constexpr int32_t THERMOSTAT_OPTION_W = 74;
constexpr int32_t THERMOSTAT_OPTIONS_Y = 200;     // In the panel
constexpr int32_t THERMOSTAT_SET_Y = 168;         // Middle of the set temperature row, in the panel
constexpr int32_t THERMOSTAT_ARROW_W = 90;        // Touch area of each arrow
constexpr int32_t THERMOSTAT_ARROW_OFFSET = 95;   // From the middle of the panel to the middle of each arrow
constexpr int32_t COVER_PRESETS = 3;

int32_t column_x(int column) {
  return MARGIN + column * (COLUMN_W + GAP);
}

// A vertical slider: a card with a line at the level of its value. Dragging moves the line; the
// value is queued when the card is released.
struct SliderCard {
  lv_obj_t *card = nullptr;
  lv_obj_t *line = nullptr;
  EntityPage::Slider id;
  int32_t max = 100;
  int32_t value = 0;
  bool dragging = false;
};

enum class ModeIcon : uint8_t {
  NONE,
  LIGHT_TO_COLOR,             // Light shows colour temperature: the button switches to colour
  LIGHT_TO_COLOR_TEMPERATURE, // Light shows colour: the button switches to colour temperature
  COVER_TO_TILT,
  COVER_TO_POSITION,
};

struct {
  lv_obj_t *screen = nullptr;
  lv_obj_t *title;
  lv_obj_t *header_separator;
  lv_obj_t *mode_button;
  lv_obj_t *mode_label;
  ModeIcon mode_icon = ModeIcon::NONE;
  lv_obj_t *message;

  lv_obj_t *light_view = nullptr;
  SliderCard light_sliders[3];
  bool light_shows_color = false; // What the second light slider edits: saturation, or colour temperature

  lv_obj_t *thermostat_view = nullptr;
  lv_obj_t *current_temperature;
  lv_obj_t *set_label;
  lv_obj_t *set_value;
  lv_obj_t *option_box[THERMOSTAT_OPTIONS];
  lv_obj_t *option_icon[THERMOSTAT_OPTIONS];
  lv_obj_t *option_value[THERMOSTAT_OPTIONS];

  lv_obj_t *cover_view = nullptr;
  lv_obj_t *preset_box[COVER_PRESETS];
  lv_obj_t *preset_label[COVER_PRESETS];
  lv_obj_t *open_box, *open_icon;
  lv_obj_t *stop_box;
  lv_obj_t *close_box, *close_icon;
  SliderCard cover_slider;
} ui;

// Set temperature: when it was sent, so the requested value is shown until the thermostat reports it.
int64_t set_temperature_sent_at = 0;
constexpr int64_t SET_TEMPERATURE_SHOW_REQUESTED_US = 5 * 1000 * 1000;
constexpr uint64_t SET_TEMPERATURE_SEND_DELAY_US = 1500 * 1000; // After the last up/down tap

// --- Drawing helpers, in screen coordinates ---
void draw_line(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t width, lv_color_t color) {
  lv_draw_line_dsc_t dsc;
  lv_draw_line_dsc_init(&dsc);
  dsc.color = color;
  dsc.width = width;
  dsc.round_start = 1;
  dsc.round_end = 1;
  dsc.p1.x = x1;
  dsc.p1.y = y1;
  dsc.p2.x = x2;
  dsc.p2.y = y2;
  lv_draw_line(layer, &dsc);
}

void draw_arc(lv_layer_t *layer, int32_t cx, int32_t cy, int32_t radius, int32_t width, int32_t start, int32_t end, lv_color_t color) {
  lv_draw_arc_dsc_t dsc;
  lv_draw_arc_dsc_init(&dsc);
  dsc.color = color;
  dsc.center.x = cx;
  dsc.center.y = cy;
  dsc.radius = radius;
  dsc.width = width;
  dsc.start_angle = start;
  dsc.end_angle = end;
  lv_draw_arc(layer, &dsc);
}

void draw_rect(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, lv_color_t color, int32_t radius = 0) {
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_color = color;
  dsc.radius = radius;
  lv_area_t area = {x1, y1, x2, y2};
  lv_draw_rect(layer, &dsc, &area);
}

void draw_circle(lv_layer_t *layer, int32_t cx, int32_t cy, int32_t radius, lv_color_t color) {
  draw_rect(layer, cx - radius, cy - radius, cx + radius, cy + radius, color, LV_RADIUS_CIRCLE);
}

lv_area_t draw_area(lv_event_t *e, lv_layer_t **layer) {
  lv_obj_t *obj = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
  *layer = lv_event_get_layer(e);
  lv_area_t coords;
  lv_obj_get_coords(obj, &coords);
  return coords;
}

// Unit vectors (* 1000) every 45 degrees, for sun rays and saturation dots
constexpr int16_t DIRECTIONS[8][2] = {{1000, 0}, {707, 707}, {0, 1000}, {-707, 707}, {-1000, 0}, {-707, -707}, {0, -1000}, {707, -707}};

void draw_sun(lv_layer_t *layer, int32_t cx, int32_t cy) {
  lv_color_t white = lv_color_hex(COLOR_FOREGROUND);
  draw_circle(layer, cx, cy, 8, white);
  for (const auto &d : DIRECTIONS) {
    draw_line(layer, cx + d[0] * 12 / 1000, cy + d[1] * 12 / 1000, cx + d[0] * 18 / 1000, cy + d[1] * 18 / 1000, 2, white);
  }
}

// Dots growing around a circle: saturation
void draw_saturation(lv_layer_t *layer, int32_t cx, int32_t cy) {
  for (int i = 0; i < 8; i++) {
    const auto &d = DIRECTIONS[(i + 2) % 8];
    draw_circle(layer, cx + d[0] * 14 / 1000, cy + d[1] * 14 / 1000, 1 + i / 2, lv_color_hex(COLOR_FOREGROUND));
  }
}

void draw_color_temperature_ring(lv_layer_t *layer, int32_t cx, int32_t cy, int32_t radius, int32_t width) {
  draw_arc(layer, cx, cy, radius, width, 90, 270, lv_color_hex(COLOR_WARM));
  draw_arc(layer, cx, cy, radius, width, 270, 90, lv_color_hex(COLOR_COLD));
}

void draw_color_ring(lv_layer_t *layer, int32_t cx, int32_t cy, int32_t radius, int32_t width) {
  for (int i = 0; i < 12; i++) {
    draw_arc(layer, cx, cy, radius, width, i * 30, i * 30 + 31, lv_color_hsv_to_rgb(i * 30, 100, 100));
  }
}

// --- Sliders ---
int32_t slider_range() {
  return CONTENT_H - 2 * SLIDER_PAD - SLIDER_LINE_H;
}

void set_slider(SliderCard &slider, int32_t value) {
  slider.value = value < 0 ? 0 : (value > slider.max ? slider.max : value);
  lv_obj_set_y(slider.line, SLIDER_PAD + ((slider.max - slider.value) * slider_range()) / slider.max);
}

void slider_event(lv_event_t *e) {
  SliderCard &slider = *static_cast<SliderCard *>(lv_event_get_user_data(e));
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
    lv_obj_get_coords(slider.card, &coords);
    slider.dragging = true;
    set_slider(slider, slider.max - ((point.y - coords.y1 - SLIDER_PAD) * slider.max) / slider_range());
    break;
  }
  case LV_EVENT_RELEASED:
  case LV_EVENT_PRESS_LOST:
    if (slider.dragging) {
      slider.dragging = false;
      EntityPage::queue_input({EntityPage::InputEvent::SLIDER_SET, slider.id, (int16_t)slider.value});
    }
    break;
  default:
    break;
  }
}

void light_card_draw(lv_event_t *e) {
  const SliderCard &slider = *static_cast<SliderCard *>(lv_event_get_user_data(e));
  lv_layer_t *layer;
  lv_area_t a = draw_area(e, &layer);
  int32_t cx = (a.x1 + a.x2) / 2;
  int32_t cy = (a.y1 + a.y2) / 2;
  switch (slider.id) {
  case EntityPage::LIGHT_BRIGHTNESS:
    draw_sun(layer, cx, cy);
    break;
  case EntityPage::LIGHT_COLOR_TEMPERATURE_OR_SATURATION:
    if (ui.light_shows_color) {
      draw_saturation(layer, cx, cy);
    } else {
      draw_color_temperature_ring(layer, cx, cy, 16, 5);
    }
    break;
  case EntityPage::LIGHT_HUE:
    draw_color_ring(layer, cx, cy, 16, 5);
    // The hues along the right edge, so the slider shows which colour each level is.
    for (int32_t y = a.y1 + SLIDER_PAD; y <= a.y2 - SLIDER_PAD; y++) {
      int32_t hue = ((a.y2 - SLIDER_PAD - y) * 359) / (a.y2 - a.y1 - 2 * SLIDER_PAD);
      draw_rect(layer, a.x2 - 12, y, a.x2 - 7, y, lv_color_hsv_to_rgb(hue, 100, 100));
    }
    break;
  default:
    break;
  }
}

void mode_button_draw(lv_event_t *e) {
  lv_layer_t *layer;
  lv_area_t a = draw_area(e, &layer);
  int32_t cx = (a.x1 + a.x2) / 2;
  int32_t cy = (a.y1 + a.y2) / 2;
  if (ui.mode_icon == ModeIcon::LIGHT_TO_COLOR) {
    draw_color_ring(layer, cx, cy, 12, 4);
  } else if (ui.mode_icon == ModeIcon::LIGHT_TO_COLOR_TEMPERATURE) {
    draw_color_temperature_ring(layer, cx, cy, 12, 4);
  }
}

// --- Building the screen ---
lv_obj_t *create_box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color = COLOR_PANEL, lv_opa_t opa = PANEL_OPA,
                     int32_t radius = RADIUS) {
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_set_scrollable(box, false);
  lv_obj_set_clickable(box, false);
  lv_obj_set_style_bg_color(box, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(box, opa, 0);
  lv_obj_set_style_radius(box, radius, 0);
  lv_obj_set_style_border_color(box, lv_color_hex(COLOR_HIGHLIGHT), 0);
  lv_obj_set_style_border_width(box, 0, 0);
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

void input_event(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  intptr_t data = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
  EntityPage::queue_input({static_cast<EntityPage::InputEvent::Type>(data >> 8), static_cast<uint8_t>(data & 0xFF), 0});
}

// Make an object queue `type` (with `index`) when tapped.
void on_tap(lv_obj_t *obj, EntityPage::InputEvent::Type type, uint8_t index = 0) {
  lv_obj_set_clickable(obj, true);
  lv_obj_add_event_cb(obj, input_event, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>((type << 8) | index)));
}

lv_obj_t *create_view() {
  lv_obj_t *view = create_box(ui.screen, 0, 0, DisplayManager::LANDSCAPE_WIDTH, DisplayManager::LANDSCAPE_HEIGHT, COLOR_PANEL, LV_OPA_TRANSP, 0);
  lv_obj_set_hidden(view, true);
  return view;
}

void create_slider(SliderCard &slider, lv_obj_t *parent, int32_t x, EntityPage::Slider id, int32_t max, lv_event_cb_t draw) {
  slider.id = id;
  slider.max = max;
  slider.card = create_box(parent, x, CONTENT_Y, COLUMN_W, CONTENT_H);
  lv_obj_set_clickable(slider.card, true);
  lv_obj_add_event_cb(slider.card, slider_event, LV_EVENT_ALL, &slider);
  if (draw != nullptr) {
    lv_obj_add_event_cb(slider.card, draw, LV_EVENT_DRAW_MAIN, &slider);
  }
  slider.line = create_box(slider.card, 8, 0, COLUMN_W - 16 - (id == EntityPage::LIGHT_HUE ? 10 : 0), SLIDER_LINE_H, COLOR_FOREGROUND, LV_OPA_COVER, 0);
  set_slider(slider, 0);
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

  lv_obj_t *header = create_box(ui.screen, MARGIN, HEADER_Y, HEADER_W, HEADER_H);
  lv_obj_t *back = create_box(header, 0, 0, HEADER_BUTTON_W, HEADER_H, COLOR_PANEL, LV_OPA_TRANSP, 0);
  lv_obj_t *back_label = create_label(back, &lv_font_montserrat_14, LV_SYMBOL_LEFT);
  lv_obj_align(back_label, LV_ALIGN_LEFT_MID, 16, 0);
  on_tap(back, EntityPage::InputEvent::BACK);

  ui.header_separator = create_box(header, HEADER_BUTTON_W - 16, 10, 1, HEADER_H - 20, COLOR_FOREGROUND, LV_OPA_40, 0);
  lv_obj_set_hidden(ui.header_separator, true);

  ui.title = create_label(header, &font_screensaver_text_18, "");
  lv_label_set_long_mode(ui.title, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_width(ui.title, HEADER_W - 2 * HEADER_BUTTON_W);
  lv_obj_align(ui.title, LV_ALIGN_LEFT_MID, HEADER_BUTTON_W, 0);

  ui.mode_button = create_box(header, HEADER_W - HEADER_BUTTON_W, 0, HEADER_BUTTON_W, HEADER_H, COLOR_PANEL, LV_OPA_TRANSP, 0);
  lv_obj_add_event_cb(ui.mode_button, mode_button_draw, LV_EVENT_DRAW_MAIN, nullptr);
  ui.mode_label = create_label(ui.mode_button, &font_entity_page_controls_40, "");
  lv_obj_center(ui.mode_label);
  on_tap(ui.mode_button, EntityPage::InputEvent::MODE);
  lv_obj_set_hidden(ui.mode_button, true);

  ui.message = create_label(ui.screen, &font_screensaver_text_18, "");
  lv_obj_set_width(ui.message, HEADER_W);
  lv_obj_set_style_text_align(ui.message, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(ui.message, LV_ALIGN_CENTER, 0, HEADER_H / 2);
}

void build_light_view() {
  if (ui.light_view != nullptr) {
    return;
  }
  ui.light_view = create_view();
  create_slider(ui.light_sliders[0], ui.light_view, column_x(0), EntityPage::LIGHT_BRIGHTNESS, 100, light_card_draw);
  create_slider(ui.light_sliders[1], ui.light_view, column_x(1), EntityPage::LIGHT_COLOR_TEMPERATURE_OR_SATURATION, 100, light_card_draw);
  create_slider(ui.light_sliders[2], ui.light_view, column_x(2), EntityPage::LIGHT_HUE, 359, light_card_draw);
}

// Y for a label so that the top of its capital letters (or digits) lands on cap_top.
int32_t label_y_for_cap_top(const lv_font_t *font, int32_t cap_top) {
  return cap_top - ((font->line_height - font->base_line) - font->cap_height);
}

// One panel, laid out like the Nextion thermostat page: the room temperature, the set temperature
// between left/right arrows, and a row of options (mode, fan, presets...), each its current icon
// over its name. Positions are relative to the panel.
void build_thermostat_view() {
  if (ui.thermostat_view != nullptr) {
    return;
  }
  ui.thermostat_view = create_view();
  lv_obj_t *panel = create_box(ui.thermostat_view, MARGIN, CONTENT_Y, HEADER_W, CONTENT_H);
  const int32_t center_x = HEADER_W / 2;

  auto centered_label = [&](const lv_font_t *font, const char *text, int32_t cap_top) {
    lv_obj_t *label = create_label(panel, font, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(label, HEADER_W - 2 * THERMOSTAT_ARROW_W);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(label, THERMOSTAT_ARROW_W, label_y_for_cap_top(font, cap_top));
    return label;
  };

  centered_label(&font_screensaver_text_18, "Room", 26);
  ui.current_temperature = centered_label(&font_thermostat_temperature_84, "", 56);

  // Set temperature, or the value of the option being edited, between the arrows
  ui.set_label = centered_label(&font_screensaver_text_15, "Set", 131);
  ui.set_value = centered_label(&font_thermostat_temperature_30, "", 157);
  for (int direction = 0; direction < 2; direction++) {
    bool up = direction == 1;
    lv_obj_t *arrow = create_box(panel, center_x + (up ? THERMOSTAT_ARROW_OFFSET - THERMOSTAT_ARROW_W / 2 : -THERMOSTAT_ARROW_OFFSET - THERMOSTAT_ARROW_W / 2),
                                 THERMOSTAT_SET_Y - 28, THERMOSTAT_ARROW_W, 56, COLOR_PANEL, LV_OPA_TRANSP, 0);
    lv_obj_center(create_label(arrow, &font_entity_page_controls_40, up ? ICON_CHEVRON_RIGHT : ICON_CHEVRON_LEFT));
    on_tap(arrow, up ? EntityPage::InputEvent::THERMOSTAT_UP : EntityPage::InputEvent::THERMOSTAT_DOWN);
  }

  // Options: each a transparent column that can be tapped, placed by the update for the number of options
  for (int i = 0; i < THERMOSTAT_OPTIONS; i++) {
    ui.option_box[i] = create_box(panel, 0, THERMOSTAT_OPTIONS_Y, THERMOSTAT_OPTION_W, CONTENT_H - THERMOSTAT_OPTIONS_Y, COLOR_PANEL, LV_OPA_TRANSP, 0);
    // Custom icons from the manager's icon pack, falling back to the built-in icons
    ui.option_icon[i] = create_label(ui.option_box[i], IconPackManager::font(&font_thermostat_icons_30, IconPackManager::THERMOSTAT_ICON_SIZE), "");
    lv_obj_align(ui.option_icon[i], LV_ALIGN_TOP_MID, 0, 4);
    ui.option_value[i] = create_label(ui.option_box[i], &font_screensaver_text_15, "");
    lv_label_set_long_mode(ui.option_value[i], LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(ui.option_value[i], THERMOSTAT_OPTION_W - 4);
    lv_obj_set_style_text_align(ui.option_value[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ui.option_value[i], LV_ALIGN_TOP_MID, 0, 38);
    on_tap(ui.option_box[i], EntityPage::InputEvent::THERMOSTAT_OPTION, i);
  }
}

void build_cover_view() {
  if (ui.cover_view != nullptr) {
    return;
  }
  ui.cover_view = create_view();
  lv_obj_t *view = ui.cover_view;
  constexpr int32_t button_h = (CONTENT_H - 2 * GAP) / 3;

  // Presets on the left
  for (int i = 0; i < COVER_PRESETS; i++) {
    ui.preset_box[i] = create_box(view, column_x(0), CONTENT_Y + i * (button_h + GAP), COLUMN_W, button_h);
    ui.preset_label[i] = create_label(ui.preset_box[i], &font_screensaver_text_18, "");
    lv_obj_center(ui.preset_label[i]);
    on_tap(ui.preset_box[i], EntityPage::InputEvent::COVER_PRESET, i);
  }

  // Open, stop and close in the middle
  ui.open_box = create_box(view, column_x(1), CONTENT_Y, COLUMN_W, button_h);
  ui.open_icon = create_label(ui.open_box, &font_entity_page_controls_40, ICON_OPEN);
  lv_obj_center(ui.open_icon);
  on_tap(ui.open_box, EntityPage::InputEvent::COVER_OPEN);
  ui.stop_box = create_box(view, column_x(1), CONTENT_Y + button_h + GAP, COLUMN_W, button_h);
  lv_obj_center(create_label(ui.stop_box, &font_entity_page_controls_40, ICON_STOP));
  on_tap(ui.stop_box, EntityPage::InputEvent::COVER_STOP);
  ui.close_box = create_box(view, column_x(1), CONTENT_Y + 2 * (button_h + GAP), COLUMN_W, button_h);
  ui.close_icon = create_label(ui.close_box, &font_entity_page_controls_40, ICON_CLOSE);
  lv_obj_center(ui.close_icon);
  on_tap(ui.close_box, EntityPage::InputEvent::COVER_CLOSE);

  // Position or tilt on the right, 100 (open) at the top
  create_slider(ui.cover_slider, view, column_x(2), EntityPage::COVER_POSITION, 100, nullptr);
}

void set_text(lv_obj_t *label, const char *text) {
  if (strcmp(lv_label_get_text(label), text) != 0) { // Unchanged text would still redraw the label
    lv_label_set_text(label, text);
  }
}

void set_visible(lv_obj_t *obj, bool visible) {
  lv_obj_set_hidden(obj, !visible);
}

// The thermostat page has a separator after the back button and its name centred; the others the name on the left.
void set_header_style(bool centered) {
  lv_obj_set_hidden(ui.header_separator, !centered);
  lv_obj_set_style_text_align(ui.title, centered ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, 0);
}

void set_mode_icon(ModeIcon icon) {
  set_visible(ui.mode_button, icon != ModeIcon::NONE);
  set_text(ui.mode_label, icon == ModeIcon::COVER_TO_TILT ? ICON_TILT_MODE : (icon == ModeIcon::COVER_TO_POSITION ? ICON_POSITION_MODE : ""));
  if (ui.mode_icon != icon) {
    ui.mode_icon = icon;
    lv_obj_invalidate(ui.mode_button); // Redraws the light mode rings
  }
}

// Show one view (or only the message, with nullptr), hiding the others.
void set_header_style(bool centered);

void show_view(lv_obj_t *view, const char *message = "") {
  for (lv_obj_t *other : {ui.light_view, ui.thermostat_view, ui.cover_view}) {
    if (other != nullptr) {
      set_visible(other, other == view);
    }
  }
  set_text(ui.message, message);
  if (view == nullptr) {
    set_mode_icon(ModeIcon::NONE);
    set_header_style(false);
  }
}

void update_slider(SliderCard &slider, int32_t value) {
  if (!slider.dragging) { // Do not move the slider under the user's finger
    set_slider(slider, value);
  }
}

void update_light(const NSPanelEntityState__Light *light, EntityPage::LightMode mode) {
  build_light_view();
  show_view(ui.light_view);
  set_text(ui.title, light->name);

  bool color = mode == EntityPage::LightMode::COLOR;
  set_mode_icon(light->can_color && light->can_color_temp ? (color ? ModeIcon::LIGHT_TO_COLOR_TEMPERATURE : ModeIcon::LIGHT_TO_COLOR) : ModeIcon::NONE);

  update_slider(ui.light_sliders[0], light->brightness);

  // Second slider: colour temperature, or saturation in colour mode. Hidden when the light can do neither.
  SliderCard &second = ui.light_sliders[1];
  if (ui.light_shows_color != color) {
    ui.light_shows_color = color;
    lv_obj_invalidate(second.card); // Redraws its icon
  }
  set_visible(second.card, color ? light->can_color : light->can_color_temp);
  update_slider(second, color ? light->saturation : light->color_temp);

  set_visible(ui.light_sliders[2].card, color && light->can_color);
  update_slider(ui.light_sliders[2], light->hue);
}

const char *cover_state_text(NSPanelEntityState__Cover__CoverState state) {
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
} // namespace

// ---------------------------------------------------------------------------
// EntityPage
// ---------------------------------------------------------------------------
void EntityPage::show(std::string state_topic) {
  esp_log_level_set("EntityPage", ConfigManager::log_level);
  ESP_LOGD("EntityPage", "Showing entity page for %s.", state_topic.c_str());

  if (EntityPage::_current_state_mutex == NULL) {
    // First time the page is shown
    EntityPage::_current_state_mutex = xSemaphoreCreateMutex();
    EntityPage::_input_queue = xQueueCreate(8, sizeof(InputEvent));
    if (EntityPage::_input_queue == NULL || xTaskCreatePinnedToCore(EntityPage::_task_handle_input, "entity_input", 4096, NULL, 3, NULL, 1) != pdPASS) {
      ESP_LOGE("EntityPage", "Failed to start input task. Entity page will not react to touch!");
    }
    esp_timer_create_args_t timer_args = {};
    timer_args.callback = &EntityPage::_set_temperature_timer_callback;
    timer_args.name = "set_temp_timer";
    if (esp_timer_create(&timer_args, &EntityPage::_set_temperature_timer) != ESP_OK) {
      ESP_LOGE("EntityPage", "Failed to create set temperature timer. A thermostat's set temperature is only sent when leaving the page.");
    }
  }

  InterfaceManager::begin_page(EntityPage::unshow);

  {
    DisplayManager::Lock lock;
    build_screen();
    set_text(ui.title, "");
    show_view(nullptr, "Loading...");
    lv_screen_load(ui.screen);
  }
  ESP_LOGD("EntityPage", "Entity page screen loaded, waiting for the entity's state.");

  // Follow the new entity. When another entity was shown, the page is still current, so unshow()
  // does not run for it: leave its topic here.
  std::string previous_topic;
  if (xSemaphoreTake(EntityPage::_current_state_mutex, portMAX_DELAY) == pdTRUE) {
    previous_topic = EntityPage::_current_entity_mqtt_topic;
    EntityPage::_current_entity_mqtt_topic = state_topic;
    EntityPage::_current_state = nullptr;
    xSemaphoreGive(EntityPage::_current_state_mutex);
  }
  if (!previous_topic.empty() && previous_topic != state_topic) {
    MqttManager::unsubscribe(previous_topic);
  }

  EntityPage::_waiting_for_first_state = true;
  EntityPage::_editing_option = -1;
  EntityPage::_editing_value = -1;
  EntityPage::_set_temperature_changed = false;
  set_temperature_sent_at = 0;
  EntityPage::_pending_position = -1;
  if (EntityPage::_set_temperature_timer != NULL) {
    esp_timer_stop(EntityPage::_set_temperature_timer);
  }

  if (!EntityPage::_mqtt_handler_registered) {
    MqttManager::register_handler(MQTT_EVENT_ANY, &EntityPage::_handle_mqtt_event, NULL);
    EntityPage::_mqtt_handler_registered = true;
  }
  MqttManager::subscribe(state_topic); // The state is retained, so it arrives right away
}

void EntityPage::unshow() {
  if (EntityPage::_mqtt_handler_registered) {
    MqttManager::unregister_handler(MQTT_EVENT_ANY, &EntityPage::_handle_mqtt_event);
    EntityPage::_mqtt_handler_registered = false;
  }
  std::string topic;
  if (EntityPage::_current_state_mutex != NULL && xSemaphoreTake(EntityPage::_current_state_mutex, portMAX_DELAY) == pdTRUE) {
    topic = EntityPage::_current_entity_mqtt_topic;
    EntityPage::_current_entity_mqtt_topic.clear();
    EntityPage::_current_state = nullptr;
    xSemaphoreGive(EntityPage::_current_state_mutex);
  }
  if (!topic.empty()) {
    MqttManager::unsubscribe(topic);
  }
  if (EntityPage::_set_temperature_timer != NULL) {
    esp_timer_stop(EntityPage::_set_temperature_timer);
  }
}

void EntityPage::refresh_icons() {
  if (ui.thermostat_view == nullptr) {
    return;
  }
  const lv_font_t *font = IconPackManager::font(&font_thermostat_icons_30, IconPackManager::THERMOSTAT_ICON_SIZE);
  for (lv_obj_t *icon : ui.option_icon) {
    lv_obj_set_style_text_font(icon, font, 0); // Stop using the previous pack's font, which is freed after this
  }
}

void EntityPage::queue_input(InputEvent event) {
  if (EntityPage::_input_queue != NULL && xQueueSend(EntityPage::_input_queue, &event, 0) != pdTRUE) {
    ESP_LOGW("EntityPage", "Input queue full, dropped touch input.");
  }
}

void EntityPage::_handle_mqtt_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id != MQTT_EVENT_DATA) {
    return;
  }
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
  // A fragment of a message larger than the MQTT buffer would unpack as a truncated state. MqttManager logs the drop.
  if (event->topic_len == 0 || event->data_len == 0 || event->data_len != event->total_data_len) [[unlikely]] {
    return;
  }

  if (xSemaphoreTake(EntityPage::_current_state_mutex, pdMS_TO_TICKS(500)) != pdPASS) [[unlikely]] {
    ESP_LOGE("EntityPage", "Failed to take mutex to update current state.");
    return;
  }
  bool ours = EntityPage::_current_entity_mqtt_topic.size() == (size_t)event->topic_len &&
              EntityPage::_current_entity_mqtt_topic.compare(0, event->topic_len, event->topic, event->topic_len) == 0;
  NSPanelEntityState *new_state = ours ? nspanel_entity_state__unpack(NULL, event->data_len, (const uint8_t *)event->data) : NULL;
  if (new_state != NULL) {
    EntityPage::_current_state = std::shared_ptr<NSPanelEntityState>(new_state, &EntityPage::_delete_nspanel_entity_state_object);
  }
  xSemaphoreGive(EntityPage::_current_state_mutex);
  if (!ours) {
    return;
  }
  if (new_state == NULL) {
    ESP_LOGE("EntityPage", "Received new state but failed to parse into protobuf object.");
    return;
  }
  ESP_LOGD("EntityPage", "Received state, entity type %d.", (int)new_state->entity_case);

  if (EntityPage::_waiting_for_first_state.exchange(false)) {
    // The page opens in the light's current mode, and on the cover's position if it has one.
    if (new_state->entity_case == NSPANEL_ENTITY_STATE__ENTITY_LIGHT) {
      EntityPage::_light_mode = new_state->light->current_light_mode == NSPANEL_ENTITY_STATE__LIGHT__LIGHT_MODE__RGB ? LightMode::COLOR : LightMode::COLOR_TEMPERATURE;
    } else if (new_state->entity_case == NSPANEL_ENTITY_STATE__ENTITY_COVER) {
      EntityPage::_cover_mode = new_state->cover->can_set_position || !new_state->cover->can_set_tilt_position ? CoverMode::POSITION : CoverMode::TILT;
    }
  }
  EntityPage::_update_display();
}

void EntityPage::_update_display() {
  std::shared_ptr<NSPanelEntityState> state = EntityPage::_get_current_state();
  if (state == nullptr) {
    return;
  }

  DisplayManager::Lock lock;
  if (ui.screen == nullptr) {
    return;
  }

  switch (state->entity_case) {
  case NSPANEL_ENTITY_STATE__ENTITY_LIGHT: {
    const NSPanelEntityState__Light *light = state->light;
    // Only a mode the light can do: colour temperature for a light without colour, and the other way round.
    LightMode mode = EntityPage::_light_mode;
    if (mode == LightMode::COLOR && !light->can_color && light->can_color_temp) {
      mode = LightMode::COLOR_TEMPERATURE;
    } else if (mode == LightMode::COLOR_TEMPERATURE && !light->can_color_temp && light->can_color) {
      mode = LightMode::COLOR;
    }
    EntityPage::_light_mode = mode;
    set_header_style(false);
    update_light(light, mode);
    break;
  }

  case NSPANEL_ENTITY_STATE__ENTITY_THERMOSTAT: {
    const NSPanelEntityState__Thermostat *thermostat = state->thermostat;
    build_thermostat_view();
    show_view(ui.thermostat_view);
    set_header_style(true);
    set_mode_icon(ModeIcon::NONE);
    set_text(ui.title, thermostat->name);

    char text[24];
    snprintf(text, sizeof(text), "%.1f°", thermostat->has_current_temperature ? thermostat->current_temperature : StatusUpdateManager::current_temperature());
    set_text(ui.current_temperature, text);

    // While an option is edited, the set area shows the value selected for it.
    int8_t editing = EntityPage::_editing_option;
    int8_t value = EntityPage::_editing_value;
    bool editing_valid = editing >= 0 && editing < (int)thermostat->n_options && value >= 0 && value < (int)thermostat->options[editing]->n_options;
    if (editing_valid) {
      set_text(ui.set_label, thermostat->options[editing]->name);
      lv_obj_set_style_text_font(ui.set_value, &font_screensaver_text_18, 0);
      set_text(ui.set_value, thermostat->options[editing]->options[value]->value);
    } else {
      // The set temperature asked for is shown until the thermostat reports it.
      float set_temperature = thermostat->set_temperature;
      if (EntityPage::_set_temperature_changed &&
          (set_temperature_sent_at == 0 || esp_timer_get_time() - set_temperature_sent_at < SET_TEMPERATURE_SHOW_REQUESTED_US) &&
          std::fabs(set_temperature - EntityPage::_requested_set_temperature) > 0.01f) {
        set_temperature = EntityPage::_requested_set_temperature;
      }
      set_text(ui.set_label, "Set");
      lv_obj_set_style_text_font(ui.set_value, &font_thermostat_temperature_30, 0);
      snprintf(text, sizeof(text), "%.1f°", set_temperature);
      set_text(ui.set_value, text);
    }

    // Centre the options the thermostat has. Each shows the icon of its current value over its name;
    // the option being edited is highlighted and shows the icon of the value selected for it.
    int shown_options = thermostat->n_options < THERMOSTAT_OPTIONS ? (int)thermostat->n_options : THERMOSTAT_OPTIONS;
    int32_t options_x = (HEADER_W - shown_options * THERMOSTAT_OPTION_W) / 2;
    for (int i = 0; i < THERMOSTAT_OPTIONS; i++) {
      bool shown = i < shown_options;
      set_visible(ui.option_box[i], shown);
      if (!shown) {
        continue;
      }
      lv_obj_set_x(ui.option_box[i], options_x + i * THERMOSTAT_OPTION_W);
      const NSPanelEntityState__Thermostat__ThermostatOption *option = thermostat->options[i];
      bool edited = editing_valid && i == editing;
      const char *icon = edited ? option->options[value]->icon : option->current_icon;
      IconPackManager::icon_shown(icon);
      set_text(ui.option_icon[i], icon);
      set_text(ui.option_value[i], option->name);
      lv_color_t color = lv_color_hex(edited ? COLOR_HIGHLIGHT : COLOR_FOREGROUND);
      lv_obj_set_style_text_color(ui.option_icon[i], color, 0);
      lv_obj_set_style_text_color(ui.option_value[i], color, 0);
    }
    break;
  }

  case NSPANEL_ENTITY_STATE__ENTITY_COVER: {
    const NSPanelEntityState__Cover *cover = state->cover;
    build_cover_view();
    show_view(ui.cover_view);
    set_header_style(false);

    CoverMode mode = EntityPage::_cover_mode;
    if (mode == CoverMode::TILT && !cover->can_set_tilt_position && !cover->can_open_tilt && !cover->can_close_tilt) {
      mode = CoverMode::POSITION;
    }
    EntityPage::_cover_mode = mode;
    bool tilt = mode == CoverMode::TILT;
    bool has_tilt = cover->can_set_tilt_position || cover->can_open_tilt || cover->can_close_tilt;
    set_mode_icon(has_tilt ? (tilt ? ModeIcon::COVER_TO_POSITION : ModeIcon::COVER_TO_TILT) : ModeIcon::NONE);

    bool can_set = tilt ? cover->can_set_tilt_position : cover->can_set_position;
    set_visible(ui.open_box, tilt ? (cover->can_open_tilt || can_set) : (cover->can_open || can_set));
    set_visible(ui.close_box, tilt ? (cover->can_close_tilt || can_set) : (cover->can_close || can_set));
    set_visible(ui.stop_box, tilt ? cover->can_stop_tilt : cover->can_stop);
    set_text(ui.open_icon, tilt ? ICON_TILT_OPEN : ICON_OPEN);
    set_text(ui.close_icon, tilt ? ICON_TILT_CLOSE : ICON_CLOSE);

    const int32_t presets[COVER_PRESETS] = {tilt ? cover->tilt_preset1 : cover->position_preset1, tilt ? cover->tilt_preset2 : cover->position_preset2,
                                            tilt ? cover->tilt_preset3 : cover->position_preset3};
    for (int i = 0; i < COVER_PRESETS; i++) {
      set_visible(ui.preset_box[i], can_set);
      char text[8];
      snprintf(text, sizeof(text), "%ld%%", (long)presets[i]);
      set_text(ui.preset_label[i], text);
    }

    // A position asked for from this page is shown until the cover reports it, or the request times out.
    bool has_reported = tilt ? cover->has_tilt_position : cover->has_position;
    int32_t reported = tilt ? cover->tilt_position : cover->position;
    int32_t pending = EntityPage::_pending_position;
    if (pending >= 0 && ((has_reported && reported == pending) || esp_timer_get_time() - EntityPage::_pending_position_requested_at > _pending_position_timeout_us)) {
      EntityPage::_pending_position = -1;
      pending = -1;
    }
    int32_t shown = pending >= 0 ? pending : (has_reported ? reported : 0);
    set_visible(ui.cover_slider.card, can_set);
    update_slider(ui.cover_slider, shown);

    std::string title = cover->name;
    title.append(" - ");
    title.append(cover_state_text(cover->state));
    if (can_set && (pending >= 0 || has_reported)) {
      title.append(" ");
      title.append(std::to_string(shown));
      title.append("%");
    }
    set_text(ui.title, title.c_str());
    break;
  }

  case NSPANEL_ENTITY_STATE__ENTITY_MEDIA_PLAYER:
    set_text(ui.title, state->media_player->name);
    show_view(nullptr, "Media players can not be controlled from the panel yet.");
    break;

  default:
    show_view(nullptr, "This entity can not be controlled from the panel.");
    break;
  }
}

void EntityPage::_task_handle_input(void *param) {
  InputEvent event;
  for (;;) {
    if (xQueueReceive(EntityPage::_input_queue, &event, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    if (!InterfaceManager::is_current_page(EntityPage::unshow)) {
      continue; // Touched just before another page was shown
    }
    if (event.type == InputEvent::BACK) {
      EntityPage::_go_back();
      continue;
    }

    // Take our own reference for the whole event, see _current_state.
    std::shared_ptr<NSPanelEntityState> state = EntityPage::_get_current_state();
    if (state == nullptr) {
      continue; // No state yet, nothing to control
    }
    switch (state->entity_case) {
    case NSPANEL_ENTITY_STATE__ENTITY_LIGHT:
      EntityPage::_handle_light_input(state, event);
      break;
    case NSPANEL_ENTITY_STATE__ENTITY_THERMOSTAT:
      EntityPage::_handle_thermostat_input(state, event);
      break;
    case NSPANEL_ENTITY_STATE__ENTITY_COVER:
      EntityPage::_handle_cover_input(state, event);
      break;
    default:
      break;
    }
  }
}

void EntityPage::_go_back() {
  std::shared_ptr<NSPanelEntityState> state = EntityPage::_get_current_state();
  if (state != nullptr && state->entity_case == NSPANEL_ENTITY_STATE__ENTITY_THERMOSTAT) {
    // Do not lose a change that has not been sent yet.
    EntityPage::_send_thermostat_option_command(state);
    EntityPage::_send_thermostat_setpoint_command(state);
  }
  EntitiesPage::show(EntitiesPage::display_type_t::ENTITIES);
}

// --- Light ---
void EntityPage::_handle_light_input(const std::shared_ptr<NSPanelEntityState> &state, const InputEvent &event) {
  if (event.type == InputEvent::MODE) {
    EntityPage::_light_mode = EntityPage::_light_mode == LightMode::COLOR ? LightMode::COLOR_TEMPERATURE : LightMode::COLOR;
    EntityPage::_update_display();
  } else if (event.type == InputEvent::SLIDER_SET) {
    EntityPage::_send_light_command(state->light, static_cast<Slider>(event.index), event.value);
  }
}

void EntityPage::_send_light_command(const NSPanelEntityState__Light *light, Slider slider, int32_t value) {
  int32_t light_id = light->light_id;
  NSPanelMQTTManagerCommand__LightCommand light_command = NSPANEL_MQTTMANAGER_COMMAND__LIGHT_COMMAND__INIT;
  light_command.light_ids = &light_id;
  light_command.n_light_ids = 1;
  switch (slider) {
  case LIGHT_BRIGHTNESS:
    light_command.has_brightness = true;
    light_command.brightness = value;
    break;
  case LIGHT_COLOR_TEMPERATURE_OR_SATURATION:
    if (EntityPage::_light_mode == LightMode::COLOR) {
      light_command.has_saturation = true;
      light_command.saturation = value;
    } else {
      light_command.has_color_temperature = true;
      light_command.color_temperature = value;
    }
    break;
  case LIGHT_HUE:
    light_command.has_hue = true;
    light_command.hue = value;
    break;
  default:
    return;
  }

  NSPanelMQTTManagerCommand cmd = NSPANEL_MQTTMANAGER_COMMAND__INIT;
  cmd.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_LIGHT_COMMAND;
  cmd.light_command = &light_command;
  EntityPage::_send_command(&cmd);
}

// --- Thermostat ---
void EntityPage::_handle_thermostat_input(const std::shared_ptr<NSPanelEntityState> &state, const InputEvent &event) {
  const NSPanelEntityState__Thermostat *thermostat = state->thermostat;
  switch (event.type) {
  case InputEvent::THERMOSTAT_OPTION: {
    if (event.index >= thermostat->n_options) {
      return;
    }
    bool was_editing_this = EntityPage::_editing_option == event.index;
    EntityPage::_send_thermostat_option_command(state); // Sends and stops editing the option edited so far
    if (!was_editing_this) {
      // Start editing the tapped option, from its current value.
      const NSPanelEntityState__Thermostat__ThermostatOption *option = thermostat->options[event.index];
      int8_t value = 0;
      for (size_t i = 0; i < option->n_options; i++) {
        if (strcmp(option->options[i]->value, option->current_value) == 0) {
          value = i;
          break;
        }
      }
      EntityPage::_editing_value = value;
      EntityPage::_editing_option = event.index;
    }
    EntityPage::_update_display();
    break;
  }

  case InputEvent::THERMOSTAT_UP:
  case InputEvent::THERMOSTAT_DOWN: {
    int direction = event.type == InputEvent::THERMOSTAT_UP ? 1 : -1;
    int8_t editing = EntityPage::_editing_option;
    if (editing >= 0 && editing < (int)thermostat->n_options) {
      // Cycle through the values of the option being edited.
      int n = thermostat->options[editing]->n_options;
      if (n > 0) {
        EntityPage::_editing_value = (EntityPage::_editing_value + direction + n) % n;
      }
    } else {
      // Change the set temperature. It is sent once it has not changed for a moment. Count from the
      // value asked for while that is still shown, otherwise from what the thermostat reports.
      bool requested_shown = EntityPage::_set_temperature_changed &&
                             (set_temperature_sent_at == 0 || esp_timer_get_time() - set_temperature_sent_at < SET_TEMPERATURE_SHOW_REQUESTED_US);
      float base = requested_shown ? EntityPage::_requested_set_temperature : thermostat->set_temperature;
      EntityPage::_requested_set_temperature = base + direction * thermostat->step_size;
      EntityPage::_set_temperature_changed = true;
      set_temperature_sent_at = 0;
      if (EntityPage::_set_temperature_timer != NULL) {
        esp_timer_stop(EntityPage::_set_temperature_timer);
        esp_timer_start_once(EntityPage::_set_temperature_timer, SET_TEMPERATURE_SEND_DELAY_US);
      }
    }
    EntityPage::_update_display();
    break;
  }

  case InputEvent::THERMOSTAT_SEND_SET_TEMPERATURE:
    EntityPage::_send_thermostat_setpoint_command(state);
    break;

  default:
    break;
  }
}

void EntityPage::_set_temperature_timer_callback(void *arg) {
  EntityPage::queue_input({InputEvent::THERMOSTAT_SEND_SET_TEMPERATURE, 0, 0});
}

void EntityPage::_send_thermostat_option_command(const std::shared_ptr<NSPanelEntityState> &state) {
  int8_t editing = EntityPage::_editing_option;
  int8_t value = EntityPage::_editing_value;
  EntityPage::_editing_option = -1;
  EntityPage::_editing_value = -1;
  const NSPanelEntityState__Thermostat *thermostat = state->thermostat;
  if (editing < 0 || editing >= (int)thermostat->n_options || value < 0 || value >= (int)thermostat->options[editing]->n_options) {
    return;
  }
  const NSPanelEntityState__Thermostat__ThermostatOption *option = thermostat->options[editing];
  if (strcmp(option->options[value]->value, option->current_value) == 0) {
    return; // Unchanged
  }

  NSPanelMQTTManagerCommand__ThermostatCommand command = NSPANEL_MQTTMANAGER_COMMAND__THERMOSTAT_COMMAND__INIT;
  command.thermostat_id = thermostat->thermostat_id;
  command.option = option->name;
  command.new_value = option->options[value]->value;

  NSPanelMQTTManagerCommand cmd = NSPANEL_MQTTMANAGER_COMMAND__INIT;
  cmd.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_THERMOSTAT_COMMAND;
  cmd.thermostat_command = &command;
  EntityPage::_send_command(&cmd);
}

void EntityPage::_send_thermostat_setpoint_command(const std::shared_ptr<NSPanelEntityState> &state) {
  if (!EntityPage::_set_temperature_changed || set_temperature_sent_at != 0) {
    return; // Nothing new to send
  }
  if (EntityPage::_set_temperature_timer != NULL) {
    esp_timer_stop(EntityPage::_set_temperature_timer);
  }

  NSPanelMQTTManagerCommand__ThermostatTemperatureCommand command = NSPANEL_MQTTMANAGER_COMMAND__THERMOSTAT_TEMPERATURE_COMMAND__INIT;
  command.thermostat_id = state->thermostat->thermostat_id;
  command.temperature = EntityPage::_requested_set_temperature;

  NSPanelMQTTManagerCommand cmd = NSPANEL_MQTTMANAGER_COMMAND__INIT;
  cmd.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_THERMOSTAT_TEMPERATURE_COMMAND;
  cmd.thermostat_temperature_command = &command;
  EntityPage::_send_command(&cmd);
  set_temperature_sent_at = esp_timer_get_time(); // Shown until the thermostat reports it, see _update_display()
}

// --- Cover ---
void EntityPage::_handle_cover_input(const std::shared_ptr<NSPanelEntityState> &state, const InputEvent &event) {
  const NSPanelEntityState__Cover *cover = state->cover;
  bool tilt = EntityPage::_cover_mode == CoverMode::TILT;
  bool can_set = tilt ? cover->can_set_tilt_position : cover->can_set_position;
  switch (event.type) {
  case InputEvent::MODE:
    EntityPage::_cover_mode = tilt ? CoverMode::POSITION : CoverMode::TILT;
    EntityPage::_pending_position = -1;
    EntityPage::_update_display();
    break;

  case InputEvent::COVER_OPEN:
    if (tilt ? cover->can_open_tilt : cover->can_open) {
      EntityPage::_send_cover_action(cover, tilt ? NSPANEL_MQTTMANAGER_COMMAND__COVER_COMMAND__ACTION__OPEN_TILT : NSPANEL_MQTTMANAGER_COMMAND__COVER_COMMAND__ACTION__OPEN);
    } else if (can_set) {
      EntityPage::_send_cover_position(cover, tilt, 100);
    }
    break;

  case InputEvent::COVER_CLOSE:
    if (tilt ? cover->can_close_tilt : cover->can_close) {
      EntityPage::_send_cover_action(cover, tilt ? NSPANEL_MQTTMANAGER_COMMAND__COVER_COMMAND__ACTION__CLOSE_TILT : NSPANEL_MQTTMANAGER_COMMAND__COVER_COMMAND__ACTION__CLOSE);
    } else if (can_set) {
      EntityPage::_send_cover_position(cover, tilt, 0);
    }
    break;

  case InputEvent::COVER_STOP:
    if (tilt ? cover->can_stop_tilt : cover->can_stop) {
      EntityPage::_send_cover_action(cover, tilt ? NSPANEL_MQTTMANAGER_COMMAND__COVER_COMMAND__ACTION__STOP_TILT : NSPANEL_MQTTMANAGER_COMMAND__COVER_COMMAND__ACTION__STOP);
    }
    break;

  case InputEvent::COVER_PRESET: {
    const int32_t presets[COVER_PRESETS] = {tilt ? cover->tilt_preset1 : cover->position_preset1, tilt ? cover->tilt_preset2 : cover->position_preset2,
                                            tilt ? cover->tilt_preset3 : cover->position_preset3};
    if (can_set && event.index < COVER_PRESETS) {
      EntityPage::_send_cover_position(cover, tilt, presets[event.index]);
    }
    break;
  }

  case InputEvent::SLIDER_SET:
    if (can_set) {
      EntityPage::_send_cover_position(cover, tilt, event.value);
    }
    break;

  default:
    break;
  }
}

void EntityPage::_send_cover_action(const NSPanelEntityState__Cover *cover, NSPanelMQTTManagerCommand__CoverCommand__Action action) {
  NSPanelMQTTManagerCommand__CoverCommand command = NSPANEL_MQTTMANAGER_COMMAND__COVER_COMMAND__INIT;
  command.cover_id = cover->cover_id;
  command.action = action;

  NSPanelMQTTManagerCommand cmd = NSPANEL_MQTTMANAGER_COMMAND__INIT;
  cmd.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_COVER_COMMAND;
  cmd.cover_command = &command;
  EntityPage::_send_command(&cmd);
}

void EntityPage::_send_cover_position(const NSPanelEntityState__Cover *cover, bool tilt, int32_t position) {
  NSPanelMQTTManagerCommand__CoverCommand command = NSPANEL_MQTTMANAGER_COMMAND__COVER_COMMAND__INIT;
  command.cover_id = cover->cover_id;
  command.has_position = !tilt;
  command.position = tilt ? 0 : position;
  command.has_tilt_position = tilt;
  command.tilt_position = tilt ? position : 0;
  EntityPage::_pending_position = position;
  EntityPage::_pending_position_requested_at = esp_timer_get_time();

  NSPanelMQTTManagerCommand cmd = NSPANEL_MQTTMANAGER_COMMAND__INIT;
  cmd.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_COVER_COMMAND;
  cmd.cover_command = &command;
  EntityPage::_send_command(&cmd);
  EntityPage::_update_display();
}

// --- Common ---
void EntityPage::_send_command(NSPanelMQTTManagerCommand *command) {
  command->nspanel_id = NSPM_ConfigManager::get_nspanel_id();
  uint32_t packed_length = nspanel_mqttmanager_command__get_packed_size(command);
  std::vector<uint8_t> buffer(packed_length); // Use vector for automatic cleanup of data when going out of scope
  size_t packed_data_size = nspanel_mqttmanager_command__pack(command, buffer.data());
  if (packed_data_size != packed_length) [[unlikely]] {
    ESP_LOGE("EntityPage", "Failed to pack protobuf command.");
    return;
  }
  if (MqttManager::publish(NSPM_ConfigManager::get_manager_command_topic(), (const char *)buffer.data(), packed_length, false) != ESP_OK) [[unlikely]] {
    ESP_LOGE("EntityPage", "Failed to send MQTT message with command payload.");
  }
}

void EntityPage::_delete_nspanel_entity_state_object(NSPanelEntityState *object) {
  nspanel_entity_state__free_unpacked(object, NULL);
}

std::shared_ptr<NSPanelEntityState> EntityPage::_get_current_state() {
  if (EntityPage::_current_state_mutex != NULL && xSemaphoreTake(EntityPage::_current_state_mutex, pdMS_TO_TICKS(5000)) == pdPASS) [[likely]] {
    std::shared_ptr<NSPanelEntityState> ret = EntityPage::_current_state;
    xSemaphoreGive(EntityPage::_current_state_mutex);
    return ret;
  }
  return nullptr;
}

std::shared_ptr<NSPanelEntityState> EntityPage::get_current_state() {
  return EntityPage::_get_current_state();
}

std::string EntityPage::get_current_state_topic() {
  std::string topic;
  if (EntityPage::_current_state_mutex != NULL && xSemaphoreTake(EntityPage::_current_state_mutex, pdMS_TO_TICKS(5000)) == pdPASS) {
    topic = EntityPage::_current_entity_mqtt_topic;
    xSemaphoreGive(EntityPage::_current_state_mutex);
  }
  return topic;
}
