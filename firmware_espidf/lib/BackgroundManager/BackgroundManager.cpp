#include <BackgroundManager.hpp>
#include <ConfigManager.hpp>
#include <DisplayManager.hpp>
#include <NSPM_ConfigManager.hpp>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <esp_littlefs.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <lvgl_private.h> // lv_image_cache_drop()
#include <strings.h>
#include <sys/stat.h>

namespace {
constexpr const char *IMAGE_PATH = "/littlefs/background.bin";
constexpr const char *IMAGE_TMP_PATH = "/littlefs/background.tmp"; // Only removed: written by earlier firmware
constexpr const char *ETAG_PATH = "/littlefs/background.etag";
// The image is the size of the display, which depends on its orientation.
uint32_t image_stride() {
  return DisplayManager::width() * 2; // RGB565
}
size_t image_size() {
  return image_stride() * DisplayManager::height();
}

uint8_t *alloc_pixels() {
  // The image is far too large for internal RAM, which Wi-Fi needs.
  return static_cast<uint8_t *>(heap_caps_malloc(image_size(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

// The ETag file is written after the image, so the image only counts as stored when both exist.
bool stored_image_exists() {
  struct stat st;
  return stat(ETAG_PATH, &st) == 0 && stat(IMAGE_PATH, &st) == 0 && (size_t)st.st_size == image_size();
}

esp_err_t http_event_handler(esp_http_client_event_t *event) {
  if (event->event_id == HTTP_EVENT_ON_HEADER && strcasecmp(event->header_key, "ETag") == 0) {
    *static_cast<std::string *>(event->user_data) = event->header_value;
  }
  return ESP_OK;
}
} // namespace

void BackgroundManager::init() {
  esp_log_level_set("BackgroundManager", ConfigManager::log_level);
  if (BackgroundManager::_task != NULL) {
    return;
  }

  uint8_t *pixels = BackgroundManager::_load_stored_image();
  if (pixels != nullptr) {
    BackgroundManager::_set_image(pixels);
    ESP_LOGI("BackgroundManager", "Loaded stored background image.");
  }

  if (xTaskCreatePinnedToCore(BackgroundManager::_task_update, "background", 6144, NULL, 1, &BackgroundManager::_task, 1) != pdPASS) {
    ESP_LOGE("BackgroundManager", "Failed to start background image task. Background image will not be updated.");
    BackgroundManager::_task = NULL;
  }
}

void BackgroundManager::request_update() {
  if (BackgroundManager::_task != NULL) {
    xTaskNotifyGive(BackgroundManager::_task);
  }
}

void BackgroundManager::attach(lv_obj_t *screen) {
  BackgroundManager::_screens.push_back(screen);
  lv_obj_set_style_bg_image_src(screen, BackgroundManager::_image, 0);
}

bool BackgroundManager::has_image() {
  return BackgroundManager::_image != nullptr;
}

void BackgroundManager::_task_update(void *param) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    BackgroundManager::_download();
  }
}

bool BackgroundManager::_download() {
  std::shared_ptr<NSPanelConfig> config;
  std::string manager_address = NSPM_ConfigManager::get_manager_address();
  if (manager_address.empty() || NSPM_ConfigManager::get_config(&config) != ESP_OK) {
    ESP_LOGW("BackgroundManager", "No manager or config yet, can not check for a background image.");
    return false;
  }

  std::string url = "http://";
  url.append(manager_address);
  url.append(":");
  url.append(std::to_string(NSPM_ConfigManager::get_manager_port()));
  url.append("/download_background/");
  url.append(std::to_string(config->nspanel_id));

  std::string stored_etag;
  bool have_stored = BackgroundManager::_read_stored_etag(&stored_etag);
  std::string new_etag;

  esp_http_client_config_t http_config = {};
  http_config.url = url.c_str();
  http_config.event_handler = http_event_handler;
  http_config.user_data = &new_etag;
  http_config.timeout_ms = 10000;
  esp_http_client_handle_t client = esp_http_client_init(&http_config);
  if (client == NULL) {
    ESP_LOGE("BackgroundManager", "Failed to create HTTP client.");
    return false;
  }
  if (have_stored) {
    esp_http_client_set_header(client, "If-None-Match", stored_etag.c_str());
  }

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE("BackgroundManager", "Failed to connect to %s: %s", url.c_str(), esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return false;
  }
  int64_t content_length = esp_http_client_fetch_headers(client);
  int status = esp_http_client_get_status_code(client);

  bool changed = false;
  if (status == 304) {
    ESP_LOGD("BackgroundManager", "Stored background image is current.");
  } else if (status == 404) {
    if (have_stored || BackgroundManager::_image != nullptr) {
      ESP_LOGI("BackgroundManager", "No background image configured, removing stored image.");
      BackgroundManager::_remove_stored_image();
      BackgroundManager::_set_image(nullptr);
      changed = true;
    }
  } else if (status == 200) {
    if (content_length != (int64_t)image_size()) {
      ESP_LOGE("BackgroundManager", "Background image has %lld bytes, expected %u (%ux%u RGB565). Ignoring it.", content_length, (unsigned)image_size(),
               (unsigned)DisplayManager::width(), (unsigned)DisplayManager::height());
    } else if (uint8_t *pixels = alloc_pixels(); pixels == nullptr) {
      ESP_LOGE("BackgroundManager", "Not enough PSRAM for a background image.");
    } else {
      size_t received = 0;
      while (received < image_size()) {
        int read = esp_http_client_read(client, (char *)pixels + received, image_size() - received);
        if (read <= 0) {
          break;
        }
        received += read;
      }
      if (received == image_size()) {
        ESP_LOGI("BackgroundManager", "Downloaded new background image.");
        BackgroundManager::_store_image(pixels, new_etag);
        BackgroundManager::_set_image(pixels);
        changed = true;
      } else {
        ESP_LOGE("BackgroundManager", "Background image download ended after %u of %u bytes.", (unsigned)received, (unsigned)image_size());
        heap_caps_free(pixels);
      }
    }
  } else {
    ESP_LOGE("BackgroundManager", "Unexpected HTTP status %d from %s.", status, url.c_str());
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return changed;
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

bool BackgroundManager::_read_stored_etag(std::string *etag) {
  if (!stored_image_exists()) {
    return false;
  }
  FILE *file = fopen(ETAG_PATH, "r");
  if (file == NULL) {
    return false;
  }
  char buffer[128] = {};
  size_t length = fread(buffer, 1, sizeof(buffer) - 1, file);
  fclose(file);
  *etag = std::string(buffer, length);
  return !etag->empty();
}

uint8_t *BackgroundManager::_load_stored_image() {
  if (!stored_image_exists()) {
    return nullptr;
  }
  uint8_t *pixels = alloc_pixels();
  if (pixels == nullptr) {
    ESP_LOGE("BackgroundManager", "Not enough PSRAM for the stored background image.");
    return nullptr;
  }
  FILE *file = fopen(IMAGE_PATH, "rb");
  size_t read = file != NULL ? fread(pixels, 1, image_size(), file) : 0;
  if (file != NULL) {
    fclose(file);
  }
  if (read != image_size()) {
    ESP_LOGE("BackgroundManager", "Failed to read stored background image.");
    heap_caps_free(pixels);
    return nullptr;
  }
  return pixels;
}

void BackgroundManager::_store_image(const uint8_t *pixels, const std::string &etag) {
  // The old image goes first: LittleFS on the Sonoff has no room for two. The image is written
  // straight to its final name (rename() is not available: CONFIG_VFS_SUPPORT_DIR is off), and
  // the ETag file after it marks it complete, see stored_image_exists(). LittleFS only commits a
  // file when it is closed, so a power loss leaves no image or a complete one, never part of one.
  // The image is shown anyway when storing fails; it is then downloaded again on the next start.
  BackgroundManager::_remove_stored_image();
  if (etag.empty()) {
    ESP_LOGW("BackgroundManager", "The manager sent no ETag for the background image, so it is not stored. It will be downloaded again after a restart.");
    return;
  }
  FILE *file = fopen(IMAGE_PATH, "wb");
  if (file == NULL) {
    BackgroundManager::_log_store_failure("create the file");
    return;
  }
  size_t written = fwrite(pixels, 1, image_size(), file);
  // fclose() writes out what fwrite() buffered, so it can fail too, e.g. when LittleFS is full.
  bool closed = fclose(file) == 0;
  if (written != image_size() || !closed) {
    BackgroundManager::_log_store_failure(written != image_size() ? "write the image" : "finish writing the image");
    remove(IMAGE_PATH);
    return;
  }
  file = fopen(ETAG_PATH, "w");
  written = file != NULL ? fwrite(etag.data(), 1, etag.size(), file) : 0;
  closed = file != NULL && fclose(file) == 0;
  if (written != etag.size() || !closed) {
    BackgroundManager::_log_store_failure("write the image's ETag");
    BackgroundManager::_remove_stored_image();
    return;
  }
  ESP_LOGI("BackgroundManager", "Stored background image in LittleFS.");
}

void BackgroundManager::_log_store_failure(const char *step) {
  int error = errno; // Before anything else can change it
  size_t total = 0, used = 0;
  esp_littlefs_info("spiffs", &total, &used);
  ESP_LOGW("BackgroundManager", "Failed to store background image in LittleFS: could not %s (%s). LittleFS uses %u of %u bytes, the image needs %u. "
           "It will be downloaded again after a restart.",
           step, strerror(error), (unsigned)used, (unsigned)total, (unsigned)image_size());
}

void BackgroundManager::_remove_stored_image() {
  remove(ETAG_PATH);
  remove(IMAGE_PATH);
  remove(IMAGE_TMP_PATH);
}
