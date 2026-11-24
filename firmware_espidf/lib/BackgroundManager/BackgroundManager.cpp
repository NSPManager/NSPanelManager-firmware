#include <BackgroundManager.hpp>
#include <ConfigManager.hpp>
#include <DisplayManager.hpp>
#include <ManagerFile.hpp>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <lvgl_private.h> // lv_image_cache_drop()

namespace {
// Stored as /littlefs/background.bin and background.etag
ManagerFile file("background", "/download_background/");

// The image is the size of the display, which depends on its orientation.
uint32_t image_stride() {
  return DisplayManager::width() * 2; // RGB565
}
size_t image_size() {
  return image_stride() * DisplayManager::height();
}
} // namespace

void BackgroundManager::init() {
  esp_log_level_set("BackgroundManager", ConfigManager::log_level);
  ::remove("/littlefs/background.tmp"); // Written by earlier firmware

  size_t size;
  uint8_t *pixels = file.load(&size, image_size());
  if (pixels != nullptr) {
    BackgroundManager::_set_image(pixels);
    ESP_LOGI("BackgroundManager", "Loaded stored background image.");
  }
  ManagerFile::register_updater(BackgroundManager::_update);
}

void BackgroundManager::attach(lv_obj_t *screen) {
  BackgroundManager::_screens.push_back(screen);
  lv_obj_set_style_bg_image_src(screen, BackgroundManager::_image, 0);
}

bool BackgroundManager::has_image() {
  return BackgroundManager::_image != nullptr;
}

void BackgroundManager::_update() {
  uint8_t *pixels;
  size_t size;
  switch (file.download(&pixels, &size, image_size(), image_size())) {
  case ManagerFile::Result::DOWNLOADED:
    ESP_LOGI("BackgroundManager", "Downloaded new background image.");
    file.store(pixels, size);
    BackgroundManager::_set_image(pixels);
    break;
  case ManagerFile::Result::REMOVED:
    ESP_LOGI("BackgroundManager", "No background image configured, removed the stored image.");
    BackgroundManager::_set_image(nullptr);
    break;
  case ManagerFile::Result::UNCHANGED:
    ESP_LOGD("BackgroundManager", "Background image is current.");
    break;
  case ManagerFile::Result::FAILED:
    break;
  }
}

void BackgroundManager::_set_image(uint8_t *pixels) {
  lv_image_dsc_t *image = nullptr;
  if (pixels != nullptr) {
    image = new lv_image_dsc_t{};
    image->header.magic = LV_IMAGE_HEADER_MAGIC;
    image->header.cf = LV_COLOR_FORMAT_RGB565;
    image->header.w = DisplayManager::width();
    image->header.h = DisplayManager::height();
    image->header.stride = image_stride();
    image->data_size = image_size();
    image->data = pixels;
  }

  DisplayManager::Lock lock;
  lv_image_dsc_t *previous = BackgroundManager::_image;
  BackgroundManager::_image = image;
  for (lv_obj_t *screen : BackgroundManager::_screens) {
    lv_obj_set_style_bg_image_src(screen, image, 0);
  }
  if (previous != nullptr) {
    // No screen refers to it any more; LVGL draws under the same lock, so it is not in use.
    lv_image_cache_drop(previous);
    heap_caps_free(const_cast<uint8_t *>(previous->data));
    delete previous;
  }
}
