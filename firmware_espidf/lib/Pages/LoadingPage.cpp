#include <DisplayManager.hpp>
#include <InterfaceManager.hpp>
#include <LoadingPage.hpp>
#include <lvgl.h>

LV_FONT_DECLARE(font_loading_logo_50);
LV_FONT_DECLARE(font_loading_subtitle_18);

// Layout of the logo, in screen pixels. Everything is placed relative to
// LOGO_X so the whole logo can be moved by changing it.
namespace {
constexpr uint32_t COLOR_BACKGROUND = 0x000000;
constexpr uint32_t COLOR_FOREGROUND = 0xFFFFFF;
constexpr uint32_t COLOR_LAMP_SHADE = 0xFFEE33;

constexpr int32_t LOGO_WIDTH = 296;
constexpr int32_t LOGO_X = (DisplayManager::WIDTH - LOGO_WIDTH) / 2;
constexpr int32_t LINE_THICKNESS = 3;

constexpr int32_t TOP_LINE_Y = 137;
constexpr int32_t TITLE_CAP_TOP_Y = 147;    // Top of the "NSPANEL" capitals
constexpr int32_t SUBTITLE_CAP_TOP_Y = 194; // Top of the "MANAGER" capitals
constexpr int32_t SUBTITLE_X = LOGO_X + 70;
constexpr int32_t BOTTOM_LINE_Y = 199; // Lines on each side of "MANAGER"

constexpr int32_t LAMP_X = LOGO_X + 256;
constexpr int32_t LAMP_SHADE_WIDTH = 36;
constexpr int32_t LAMP_SHADE_Y = 140;

constexpr int32_t STATUS_X = LOGO_X + 8;
constexpr int32_t STATUS_Y = 217;
constexpr int32_t SECONDARY_Y = 243;

lv_obj_t *_screen = nullptr;
lv_obj_t *_loading_text = nullptr;
lv_obj_t *_secondary_text = nullptr;
lv_obj_t *_lamp_shade = nullptr;

// Y position for a label so that the top of its capital letters lands on cap_top_y.
int32_t label_y_for_cap_top(const lv_font_t *font, int32_t cap_top_y) {
  int32_t ascent = font->line_height - font->base_line;
  return cap_top_y - (ascent - font->cap_height);
}

lv_obj_t *create_rect(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color, int32_t radius = 0) {
  lv_obj_t *rect = lv_obj_create(parent);
  lv_obj_remove_style_all(rect);
  lv_obj_set_scrollable(rect, false);
  lv_obj_set_clickable(rect, false);
  lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(rect, lv_color_hex(color), 0);
  lv_obj_set_style_radius(rect, radius, 0);
  lv_obj_set_pos(rect, x, y);
  lv_obj_set_size(rect, w, h);
  return rect;
}

lv_obj_t *create_label(lv_obj_t *parent, const lv_font_t *font, const char *text) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(COLOR_FOREGROUND), 0);
  lv_label_set_text(label, text);
  return label;
}

void create_lamp(lv_obj_t *parent) {
  // Shade: the top half of a circle. The circle is placed in a container half its
  // height, which clips the bottom half away.
  lv_obj_t *shade_clip = lv_obj_create(parent);
  lv_obj_remove_style_all(shade_clip);
  lv_obj_set_scrollable(shade_clip, false);
  lv_obj_set_clickable(shade_clip, false);
  lv_obj_set_pos(shade_clip, LAMP_X, LAMP_SHADE_Y);
  lv_obj_set_size(shade_clip, LAMP_SHADE_WIDTH, LAMP_SHADE_WIDTH / 2);
  _lamp_shade = create_rect(shade_clip, 0, 0, LAMP_SHADE_WIDTH, LAMP_SHADE_WIDTH, COLOR_LAMP_SHADE, LV_RADIUS_CIRCLE);

  // Pole from the shade down to the base, which stands on the right-hand line.
  constexpr int32_t pole_x = LAMP_X + LAMP_SHADE_WIDTH / 2 - 1;
  constexpr int32_t pole_top = LAMP_SHADE_Y + LAMP_SHADE_WIDTH / 2;
  constexpr int32_t base_height = 4;
  constexpr int32_t base_width = 16;
  create_rect(parent, pole_x, pole_top, 3, BOTTOM_LINE_Y - base_height - pole_top, COLOR_FOREGROUND);
  create_rect(parent, LAMP_X + (LAMP_SHADE_WIDTH - base_width) / 2, BOTTOM_LINE_Y - base_height, base_width, base_height, COLOR_FOREGROUND);
}

// Build the screen. Call with the LVGL lock held.
void build_screen() {
  if (_screen != nullptr) {
    return;
  }

  _screen = lv_obj_create(NULL);
  lv_obj_set_scrollable(_screen, false);
  lv_obj_set_style_bg_color(_screen, lv_color_hex(COLOR_BACKGROUND), 0);
  lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);

  // Line above "NSPANEL", ending where the lamp begins.
  create_rect(_screen, LOGO_X, TOP_LINE_Y, LAMP_X - LOGO_X - 6, LINE_THICKNESS, COLOR_FOREGROUND);

  lv_obj_t *title = create_label(_screen, &font_loading_logo_50, "NSPANEL");
  lv_obj_set_pos(title, LOGO_X, label_y_for_cap_top(&font_loading_logo_50, TITLE_CAP_TOP_Y));

  lv_obj_t *subtitle = create_label(_screen, &font_loading_subtitle_18, "MANAGER");
  lv_obj_set_pos(subtitle, SUBTITLE_X, label_y_for_cap_top(&font_loading_subtitle_18, SUBTITLE_CAP_TOP_Y));

  // Lines on each side of "MANAGER". The right one runs under the lamp.
  lv_obj_update_layout(subtitle);
  int32_t subtitle_end = SUBTITLE_X + lv_obj_get_width(subtitle);
  create_rect(_screen, LOGO_X, BOTTOM_LINE_Y, SUBTITLE_X - LOGO_X - 8, LINE_THICKNESS, COLOR_FOREGROUND);
  create_rect(_screen, subtitle_end + 8, BOTTOM_LINE_Y, LOGO_X + LOGO_WIDTH - subtitle_end - 8, LINE_THICKNESS, COLOR_FOREGROUND);

  create_lamp(_screen);

  int32_t text_width = DisplayManager::WIDTH - STATUS_X - 16;
  _loading_text = create_label(_screen, &lv_font_montserrat_14, "");
  lv_label_set_long_mode(_loading_text, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_width(_loading_text, text_width);
  lv_obj_set_pos(_loading_text, STATUS_X, STATUS_Y);

  _secondary_text = create_label(_screen, &lv_font_montserrat_14, "");
  lv_label_set_long_mode(_secondary_text, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_width(_secondary_text, text_width);
  lv_obj_set_pos(_secondary_text, STATUS_X, SECONDARY_Y);
}

void set_shade_opa(void *obj, int32_t value) {
  lv_obj_set_style_bg_opa(static_cast<lv_obj_t *>(obj), (lv_opa_t)value, 0);
}

// Slowly pulse the lamp shade so it is visible that the panel has not hung.
// Only the shade is redrawn, which keeps the traffic to the display small.
void start_lamp_animation() {
  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, _lamp_shade);
  lv_anim_set_exec_cb(&anim, set_shade_opa);
  lv_anim_set_values(&anim, LV_OPA_40, LV_OPA_COVER);
  lv_anim_set_duration(&anim, 1000);
  lv_anim_set_reverse_duration(&anim, 1000);
  lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
  lv_anim_start(&anim);
}

void stop_lamp_animation() {
  lv_anim_delete(_lamp_shade, set_shade_opa);
  lv_obj_set_style_bg_opa(_lamp_shade, LV_OPA_COVER, 0);
}
} // namespace

void LoadingPage::show() {
  if (LoadingPage::showing()) {
    return; // Do now show page multiple times.
  }

  InterfaceManager::begin_page(LoadingPage::unshow);
  DisplayManager::Lock lock;
  build_screen();
  stop_lamp_animation(); // In case an earlier unshow has not run yet
  start_lamp_animation();
  lv_screen_load(_screen);
}

void LoadingPage::unshow() {
  DisplayManager::Lock lock;
  if (_screen != nullptr) {
    stop_lamp_animation();
  }
}

bool LoadingPage::showing() {
  return InterfaceManager::is_current_page(LoadingPage::unshow);
}

void LoadingPage::set_loading_text(std::string text) {
  DisplayManager::Lock lock;
  build_screen();
  if (text != lv_label_get_text(_loading_text)) { // Unchanged text would still redraw the label
    lv_label_set_text(_loading_text, text.c_str());
  }
}

void LoadingPage::set_secondary_text(std::string text) {
  DisplayManager::Lock lock;
  build_screen();
  if (text != lv_label_get_text(_secondary_text)) { // Unchanged text would still redraw the label
    lv_label_set_text(_secondary_text, text.c_str());
  }
}
