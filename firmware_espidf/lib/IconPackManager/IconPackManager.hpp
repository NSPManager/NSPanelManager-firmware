#pragma once
#include <cstdint>
#include <lvgl.h>
#include <vector>

/**
 * Custom entity and scene icons, chosen in the manager.
 *
 * The manager gives each custom icon in use a code point in Unicode's private use area
 * (U+E000-U+F8FF) and sends that character as the entity's icon, as it sends "s" or "w" for the
 * built-in icons. It renders all custom icons into an icon pack, downloaded from
 *   GET http://<manager>:<port>/download_icon_pack/<nspanel_id>?sizes=48,28,30
 * (see ManagerFile; 404: no custom icons). The pack holds one font per size asked for. Each
 * becomes an LVGL font that falls back to the built-in icon font of the same size, so labels show
 * custom and built-in icons alike.
 *
 * Pack format, little endian:
 *   Header, 12 bytes:
 *     char     magic[4]     "NSIP"
 *     uint8_t  version      1
 *     uint8_t  font_count
 *     uint16_t glyph_count  Glyphs in each font: the same icons at every size
 *     uint32_t reserved     0
 *   Then font_count fonts, each:
 *     uint16_t size         Size in pixels the icons were rendered at, as asked for in "sizes"
 *     uint16_t line_height  Pixels
 *     int16_t  base_line    Pixels from the bottom of the line to the baseline
 *     uint16_t reserved     0
 *     glyph_count glyphs, ascending by code point, 16 bytes each:
 *       uint32_t code_point    U+E000-U+F8FF
 *       uint32_t bitmap_offset Into this font's bitmap data
 *       uint16_t adv_w         Advance width in 1/16 pixels
 *       uint8_t  box_w, box_h  Bitmap size in pixels
 *       int8_t   ofs_x, ofs_y  Bitmap position: x from the pen, y from the baseline to its bottom
 *       uint16_t reserved      0
 *     uint32_t bitmap_size
 *     bitmap_size bytes: each glyph's box_w * box_h pixels, 4 bits each (high nibble first), rows
 *     packed without padding, as LVGL's lv_font_fmt_txt fonts with bitmap_format 0
 */
class IconPackManager {
public:
  // Icon sizes the pages draw, and so the sizes asked for in the pack.
  static constexpr uint16_t ENTITY_ICON_SIZE_LARGE = 48;  // Entities page, 4 entities per page
  static constexpr uint16_t ENTITY_ICON_SIZE_SMALL = 28;  // Entities page, 8 and 12 entities per page
  static constexpr uint16_t THERMOSTAT_ICON_SIZE = 30;    // Thermostat options on the entity page

  /**
   * Load the stored pack and register for downloads. Call once LittleFS is mounted, before
   * ManagerFile::request_updates().
   */
  static void init();

  /**
   * Call on_change with the LVGL lock held when the pack changes, after the fonts returned by
   * font() were replaced: it must stop using fonts got before. Call before init().
   */
  static void add_on_change(void (*on_change)());

  /**
   * The font to draw icons of this size in: the pack's font, falling back to builtin, or builtin
   * when the pack has no font of this size. Call with the LVGL lock held.
   */
  static const lv_font_t *font(const lv_font_t *builtin, uint16_t size);

  /**
   * An icon is about to be shown. If it is a custom icon the pack does not have, the manager has a
   * newer pack (an icon was chosen since), so ask for it. At most every 30 seconds. Call with the
   * LVGL lock held.
   */
  static void icon_shown(const char *icon);

private:
  struct Pack;

  /**
   * Download the pack if it has changed, and use it. Runs on the ManagerFile task.
   */
  static void _update();

  /**
   * Parse a pack into fonts. Takes ownership of data (PSRAM). nullptr if it is not a valid pack.
   */
  static Pack *_parse(uint8_t *data, size_t size);
  static void _free(Pack *pack);

  /**
   * Use this pack (nullptr for none) and free the previous one.
   */
  static void _set_pack(Pack *pack);

  static inline Pack *_pack = nullptr; // Guarded by the LVGL lock
  static inline int64_t _last_requested_at = 0;
  static inline std::vector<void (*)()> _on_change;
};
