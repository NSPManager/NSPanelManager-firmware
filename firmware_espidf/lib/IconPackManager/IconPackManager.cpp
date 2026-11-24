#include <ConfigManager.hpp>
#include <DisplayManager.hpp>
#include <IconPackManager.hpp>
#include <ManagerFile.hpp>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <string>

namespace {
// Stored as /littlefs/icon_pack.bin and icon_pack.etag
ManagerFile file("icon_pack", "/download_icon_pack/");

constexpr size_t MAX_PACK_SIZE = 256 * 1024;
constexpr size_t HEADER_SIZE = 12;
constexpr size_t FONT_HEADER_SIZE = 8;
constexpr size_t GLYPH_SIZE = 16;
constexpr uint32_t PRIVATE_USE_FIRST = 0xE000;
constexpr uint32_t PRIVATE_USE_LAST = 0xF8FF;

uint16_t get16(const uint8_t *p) {
  return p[0] | (p[1] << 8);
}
uint32_t get32(const uint8_t *p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

// One font of the pack, as LVGL needs it. The bitmaps stay in the pack's data.
struct PackFont {
  uint16_t size;
  lv_font_t font;
  lv_font_fmt_txt_dsc_t dsc;
  lv_font_fmt_txt_cmap_t cmap;
  lv_font_fmt_txt_glyph_dsc_t *glyphs; // glyph_count + 1: id 0 is reserved
  uint16_t *unicode_list;              // Code point - range start, per glyph
};
} // namespace

struct IconPackManager::Pack {
  uint8_t *data; // The pack as downloaded, PSRAM
  uint8_t font_count;
  PackFont *fonts;
};

void IconPackManager::add_on_change(void (*on_change)()) {
  IconPackManager::_on_change.push_back(on_change);
}

void IconPackManager::init() {
  esp_log_level_set("IconPackManager", ConfigManager::log_level);
  file.set_url_query("?sizes=" + std::to_string(ENTITY_ICON_SIZE_LARGE) + "," + std::to_string(ENTITY_ICON_SIZE_SMALL) + "," +
                     std::to_string(THERMOSTAT_ICON_SIZE));

  size_t size;
  uint8_t *data = file.load(&size, 0);
  if (data != nullptr) {
    Pack *pack = IconPackManager::_parse(data, size);
    if (pack != nullptr) {
      IconPackManager::_set_pack(pack);
      ESP_LOGI("IconPackManager", "Loaded stored icon pack.");
    } else {
      file.remove();
    }
  }
  ManagerFile::register_updater(IconPackManager::_update);
}

const lv_font_t *IconPackManager::font(const lv_font_t *builtin, uint16_t size) {
  Pack *pack = IconPackManager::_pack;
  for (int i = 0; pack != nullptr && i < pack->font_count; i++) {
    if (pack->fonts[i].size == size) {
      pack->fonts[i].font.fallback = builtin; // Built-in icons, such as "s" for a switch
      return &pack->fonts[i].font;
    }
  }
  return builtin;
}

void IconPackManager::icon_shown(const char *icon) {
  // Custom icons are U+E000-U+F8FF, three bytes in UTF-8: 0xEE or 0xEF followed by two continuation bytes.
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(icon);
  if (icon == nullptr || (bytes[0] != 0xEE && bytes[0] != 0xEF) || (bytes[1] & 0xC0) != 0x80 || (bytes[2] & 0xC0) != 0x80) {
    return; // Not a custom icon
  }
  uint32_t code_point = ((bytes[0] & 0x0F) << 12) | ((bytes[1] & 0x3F) << 6) | (bytes[2] & 0x3F);
  if (code_point < PRIVATE_USE_FIRST || code_point > PRIVATE_USE_LAST) {
    return;
  }

  Pack *pack = IconPackManager::_pack;
  if (pack != nullptr) {
    const PackFont &font = pack->fonts[0]; // Every font has the same icons
    for (uint16_t i = 0; i < font.cmap.list_length; i++) {
      if (font.cmap.range_start + font.unicode_list[i] == code_point) {
        return; // In the pack
      }
    }
  }

  int64_t now = esp_timer_get_time();
  if (IconPackManager::_last_requested_at != 0 && now - IconPackManager::_last_requested_at < 30 * 1000 * 1000) {
    return;
  }
  IconPackManager::_last_requested_at = now;
  ESP_LOGI("IconPackManager", "Custom icon U+%04lX is not in the icon pack, checking the manager for a new one.", (unsigned long)code_point);
  ManagerFile::request_updates();
}

void IconPackManager::_update() {
  uint8_t *data;
  size_t size;
  switch (file.download(&data, &size, 0, MAX_PACK_SIZE)) {
  case ManagerFile::Result::DOWNLOADED: {
    Pack *pack = IconPackManager::_parse(data, size); // Takes data, also when it fails
    if (pack == nullptr) {
      ESP_LOGE("IconPackManager", "Downloaded icon pack is not valid, ignoring it.");
      return;
    }
    ESP_LOGI("IconPackManager", "Downloaded new icon pack.");
    file.store(pack->data, size);
    IconPackManager::_set_pack(pack);
    break;
  }
  case ManagerFile::Result::REMOVED:
    ESP_LOGI("IconPackManager", "No custom icons any more, removed the stored icon pack.");
    IconPackManager::_set_pack(nullptr);
    break;
  case ManagerFile::Result::UNCHANGED:
    ESP_LOGD("IconPackManager", "Icon pack is current.");
    break;
  case ManagerFile::Result::FAILED:
    break;
  }
}

IconPackManager::Pack *IconPackManager::_parse(uint8_t *data, size_t size) {
  auto fail = [&](const char *reason) -> Pack * {
    ESP_LOGE("IconPackManager", "Invalid icon pack: %s.", reason);
    heap_caps_free(data);
    return nullptr;
  };
  if (size < HEADER_SIZE || memcmp(data, "NSIP", 4) != 0) {
    return fail("not an icon pack");
  }
  if (data[4] != 1) {
    return fail("unknown version");
  }
  uint8_t font_count = data[5];
  uint16_t glyph_count = get16(data + 6);
  if (font_count == 0 || glyph_count == 0) {
    return fail("no fonts or no glyphs");
  }

  Pack *pack = new Pack{data, font_count, new PackFont[font_count]{}};
  size_t offset = HEADER_SIZE;
  for (int f = 0; f < font_count; f++) {
    PackFont &font = pack->fonts[f];
    if (offset + FONT_HEADER_SIZE + glyph_count * GLYPH_SIZE + 4 > size) {
      IconPackManager::_free(pack);
      ESP_LOGE("IconPackManager", "Invalid icon pack: truncated.");
      return nullptr;
    }
    const uint8_t *header = data + offset;
    font.size = get16(header);
    int32_t line_height = get16(header + 2);
    int32_t base_line = (int16_t)get16(header + 4);
    const uint8_t *glyph_table = header + FONT_HEADER_SIZE;
    uint32_t bitmap_size = get32(glyph_table + glyph_count * GLYPH_SIZE);
    const uint8_t *bitmaps = glyph_table + glyph_count * GLYPH_SIZE + 4;
    offset += FONT_HEADER_SIZE + glyph_count * GLYPH_SIZE + 4 + bitmap_size;
    if (offset > size) {
      IconPackManager::_free(pack);
      ESP_LOGE("IconPackManager", "Invalid icon pack: truncated bitmaps.");
      return nullptr;
    }

    font.glyphs = static_cast<lv_font_fmt_txt_glyph_dsc_t *>(heap_caps_calloc(glyph_count + 1, sizeof(lv_font_fmt_txt_glyph_dsc_t), MALLOC_CAP_SPIRAM));
    font.unicode_list = static_cast<uint16_t *>(heap_caps_calloc(glyph_count, sizeof(uint16_t), MALLOC_CAP_SPIRAM));
    if (font.glyphs == nullptr || font.unicode_list == nullptr) {
      IconPackManager::_free(pack);
      ESP_LOGE("IconPackManager", "Not enough PSRAM for the icon pack.");
      return nullptr;
    }

    uint32_t first = get32(glyph_table);
    uint32_t previous = 0;
    for (int g = 0; g < glyph_count; g++) {
      const uint8_t *glyph = glyph_table + g * GLYPH_SIZE;
      uint32_t code_point = get32(glyph);
      uint32_t bitmap_offset = get32(glyph + 4);
      uint8_t box_w = glyph[10], box_h = glyph[11];
      if (code_point < PRIVATE_USE_FIRST || code_point > PRIVATE_USE_LAST || (g > 0 && code_point <= previous) ||
          bitmap_offset + (box_w * box_h + 1) / 2 > bitmap_size) {
        IconPackManager::_free(pack);
        ESP_LOGE("IconPackManager", "Invalid icon pack: bad glyph %d.", g);
        return nullptr;
      }
      previous = code_point;
      lv_font_fmt_txt_glyph_dsc_t &dsc = font.glyphs[g + 1];
      // bitmap_index is relative to dsc.glyph_bitmap, which points at this font's bitmaps
      dsc.bitmap_index = bitmap_offset;
      dsc.adv_w = get16(glyph + 8);
      dsc.box_w = box_w;
      dsc.box_h = box_h;
      dsc.ofs_x = (int8_t)glyph[12];
      dsc.ofs_y = (int8_t)glyph[13];
      font.unicode_list[g] = code_point - first;
    }

    font.cmap = {};
    font.cmap.range_start = first;
    font.cmap.range_length = previous - first + 1;
    font.cmap.glyph_id_start = 1;
    font.cmap.unicode_list = font.unicode_list;
    font.cmap.glyph_id_ofs_list = NULL;
    font.cmap.list_length = glyph_count;
    font.cmap.type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY;

    font.dsc = {};
    font.dsc.glyph_bitmap = bitmaps;
    font.dsc.glyph_dsc = font.glyphs;
    font.dsc.cmaps = &font.cmap;
    font.dsc.cmap_num = 1;
    font.dsc.bpp = 4;
    font.dsc.bitmap_format = 0;

    font.font = {};
    font.font.get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt;
    font.font.get_glyph_bitmap = lv_font_get_bitmap_fmt_txt;
    font.font.line_height = line_height;
    font.font.base_line = base_line;
    font.font.subpx = LV_FONT_SUBPX_NONE;
    font.font.static_bitmap = 1;
    font.font.dsc = &font.dsc;
  }
  ESP_LOGI("IconPackManager", "Icon pack has %u icons in %u sizes.", glyph_count, font_count);
  return pack;
}

void IconPackManager::_free(Pack *pack) {
  if (pack == nullptr) {
    return;
  }
  for (int f = 0; f < pack->font_count; f++) {
    heap_caps_free(pack->fonts[f].glyphs);
    heap_caps_free(pack->fonts[f].unicode_list);
  }
  delete[] pack->fonts;
  heap_caps_free(pack->data);
  delete pack;
}

void IconPackManager::_set_pack(Pack *pack) {
  DisplayManager::Lock lock;
  Pack *previous = IconPackManager::_pack;
  IconPackManager::_pack = pack;
  for (auto on_change : IconPackManager::_on_change) {
    on_change(); // Stops using the previous pack's fonts
  }
  IconPackManager::_free(previous);
}
