#include <BackgroundManager.hpp>
#include <ConfigManager.hpp>
#include <DisplayManager.hpp>
#include <NSPM_ConfigManager.hpp>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <lvgl_private.h> // lv_image_cache_drop()
#include <strings.h>
#include <sys/stat.h>

namespace {
constexpr const char *IMAGE_PATH = "/littlefs/background.bin";
constexpr const char *IMAGE_TMP_PATH = "/littlefs/background.tmp";
constexpr const char *ETAG_PATH = "/littlefs/background.etag";
constexpr uint32_t IMAGE_STRIDE = DisplayManager::WIDTH * 2; // RGB565
constexpr size_t IMAGE_SIZE = IMAGE_STRIDE * DisplayManager::HEIGHT;

uint8_t *alloc_pixels() {
  // The image is far too large for internal RAM, which Wi-Fi needs.
  return static_cast<uint8_t *>(heap_caps_malloc(IMAGE_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

bool stored_image_exists() {
  struct stat st;
  return stat(IMAGE_PATH, &st) == 0 && (size_t)st.st_size == IMAGE_SIZE;
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
    if (content_length != (int64_t)IMAGE_SIZE) {
      ESP_LOGE("BackgroundManager", "Background image has %lld bytes, expected %u (%ux%u RGB565). Ignoring it.", content_length, (unsigned)IMAGE_SIZE,
               (unsigned)DisplayManager::WIDTH, (unsigned)DisplayManager::HEIGHT);
    } else if (uint8_t *pixels = alloc_pixels(); pixels == nullptr) {
      ESP_LOGE("BackgroundManager", "Not enough PSRAM for a background image.");
    } else {
      size_t received = 0;
      while (received < IMAGE_SIZE) {
        int read = esp_http_client_read(client, (char *)pixels + received, IMAGE_SIZE - received);
        if (read <= 0) {
          break;
        }
        received += read;
      }
      if (received == IMAGE_SIZE) {
        ESP_LOGI("BackgroundManager", "Downloaded new background image.");
        BackgroundManager::_store_image(pixels, new_etag);
        BackgroundManager::_set_image(pixels);
        changed = true;
      } else {
        ESP_LOGE("BackgroundManager", "Background image download ended after %u of %u bytes.", (unsigned)received, (unsigned)IMAGE_SIZE);
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
    image->header.w = DisplayManager::WIDTH;
    image->header.h = DisplayManager::HEIGHT;
    image->header.stride = IMAGE_STRIDE;
    image->data_size = IMAGE_SIZE;
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
  size_t read = file != NULL ? fread(pixels, 1, IMAGE_SIZE, file) : 0;
  if (file != NULL) {
    fclose(file);
  }
  if (read != IMAGE_SIZE) {
    ESP_LOGE("BackgroundManager", "Failed to read stored background image.");
    heap_caps_free(pixels);
    return nullptr;
  }
  return pixels;
}

void BackgroundManager::_store_image(const uint8_t *pixels, const std::string &etag) {
  // The old image goes first: LittleFS on the Sonoff has no room for two. The new image is
  // written to a temporary file, so a power loss never leaves a partial image behind; at
  // worst there is no stored image and it is downloaded again.
  BackgroundManager::_remove_stored_image();
  FILE *file = fopen(IMAGE_TMP_PATH, "wb");
  size_t written = file != NULL ? fwrite(pixels, 1, IMAGE_SIZE, file) : 0;
  if (file != NULL) {
    fclose(file);
  }
  if (written != IMAGE_SIZE) {
    // Shown anyway, but downloaded again on the next start.
    ESP_LOGW("BackgroundManager", "Failed to store background image in LittleFS (full?). It will be downloaded again after a restart.");
    remove(IMAGE_TMP_PATH);
    return;
  }
  if (rename(IMAGE_TMP_PATH, IMAGE_PATH) != 0) {
    ESP_LOGW("BackgroundManager", "Failed to store background image in LittleFS.");
    remove(IMAGE_TMP_PATH);
    return;
  }
  if (!etag.empty()) {
    file = fopen(ETAG_PATH, "w");
    if (file != NULL) {
      fwrite(etag.data(), 1, etag.size(), file);
      fclose(file);
    }
  }
}

void BackgroundManager::_remove_stored_image() {
  remove(ETAG_PATH);
  remove(IMAGE_PATH);
  remove(IMAGE_TMP_PATH);
}
