#include <ManagerFile.hpp>
#include <NSPM_ConfigManager.hpp>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_littlefs.h>
#include <esp_log.h>
#include <strings.h>
#include <sys/stat.h>

namespace {
esp_err_t http_event_handler(esp_http_client_event_t *event) {
  if (event->event_id == HTTP_EVENT_ON_HEADER && strcasecmp(event->header_key, "ETag") == 0) {
    *static_cast<std::string *>(event->user_data) = event->header_value;
  }
  return ESP_OK;
}
} // namespace

ManagerFile::ManagerFile(const char *name, const char *url_path) {
  this->_path = std::string("/littlefs/") + name + ".bin";
  this->_etag_path = std::string("/littlefs/") + name + ".etag";
  this->_url_path = url_path;
}

bool ManagerFile::_stored_file_exists(size_t expected_size) {
  struct stat st;
  return stat(this->_etag_path.c_str(), &st) == 0 && stat(this->_path.c_str(), &st) == 0 && st.st_size > 0 &&
         (expected_size == 0 || (size_t)st.st_size == expected_size);
}

ManagerFile::Result ManagerFile::download(uint8_t **data, size_t *size, size_t expected_size, size_t max_size) {
  *data = nullptr;
  *size = 0;
  std::shared_ptr<NSPanelConfig> config;
  std::string manager_address = NSPM_ConfigManager::get_manager_address();
  if (manager_address.empty() || NSPM_ConfigManager::get_config(&config) != ESP_OK) {
    ESP_LOGW("ManagerFile", "No manager or config yet, can not check for %s.", this->_path.c_str());
    return Result::UNCHANGED;
  }

  std::string url = "http://" + manager_address + ":" + std::to_string(NSPM_ConfigManager::get_manager_port()) + this->_url_path +
                    std::to_string(config->nspanel_id) + this->_url_query;

  std::string stored_etag;
  if (this->_stored_file_exists(expected_size)) {
    FILE *file = fopen(this->_etag_path.c_str(), "r");
    if (file != NULL) {
      char buffer[128] = {};
      size_t length = fread(buffer, 1, sizeof(buffer) - 1, file);
      fclose(file);
      stored_etag.assign(buffer, length);
    }
  }
  this->_downloaded_etag.clear();

  esp_http_client_config_t http_config = {};
  http_config.url = url.c_str();
  http_config.event_handler = http_event_handler;
  http_config.user_data = &this->_downloaded_etag;
  http_config.timeout_ms = 10000;
  esp_http_client_handle_t client = esp_http_client_init(&http_config);
  if (client == NULL) {
    ESP_LOGE("ManagerFile", "Failed to create HTTP client.");
    return Result::FAILED;
  }
  if (!stored_etag.empty()) {
    esp_http_client_set_header(client, "If-None-Match", stored_etag.c_str());
  }

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE("ManagerFile", "Failed to connect to %s: %s", url.c_str(), esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return Result::FAILED;
  }
  int64_t content_length = esp_http_client_fetch_headers(client);
  int status = esp_http_client_get_status_code(client);

  Result result = Result::FAILED;
  if (status == 304) {
    result = Result::UNCHANGED;
  } else if (status == 404) {
    bool had_file = this->_stored_file_exists(0);
    this->remove();
    result = had_file ? Result::REMOVED : Result::UNCHANGED;
  } else if (status == 200) {
    if (content_length <= 0 || (size_t)content_length > max_size || (expected_size != 0 && (size_t)content_length != expected_size)) {
      ESP_LOGE("ManagerFile", "%s has %lld bytes, expected %u (at most %u). Ignoring it.", url.c_str(), content_length, (unsigned)expected_size,
               (unsigned)max_size);
    } else if (uint8_t *buffer = static_cast<uint8_t *>(heap_caps_malloc(content_length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)); buffer == nullptr) {
      ESP_LOGE("ManagerFile", "Not enough PSRAM for %s (%lld bytes).", url.c_str(), content_length);
    } else {
      size_t received = 0;
      while (received < (size_t)content_length) {
        int read = esp_http_client_read(client, (char *)buffer + received, content_length - received);
        if (read <= 0) {
          break;
        }
        received += read;
      }
      if (received == (size_t)content_length) {
        *data = buffer;
        *size = received;
        result = Result::DOWNLOADED;
      } else {
        ESP_LOGE("ManagerFile", "Download of %s ended after %u of %lld bytes.", url.c_str(), (unsigned)received, content_length);
        heap_caps_free(buffer);
      }
    }
  } else {
    ESP_LOGE("ManagerFile", "Unexpected HTTP status %d from %s.", status, url.c_str());
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return result;
}

void ManagerFile::store(const uint8_t *data, size_t size) {
  // The old file goes first: LittleFS on the Sonoff may not have room for two. The ETag file is
  // written after the file and marks it complete, see _stored_file_exists().
  this->remove();
  if (this->_downloaded_etag.empty()) {
    ESP_LOGW("ManagerFile", "The manager sent no ETag for %s, so it is not stored. It will be downloaded again after a restart.", this->_path.c_str());
    return;
  }
  FILE *file = fopen(this->_path.c_str(), "wb");
  if (file == NULL) {
    this->_log_store_failure("create the file", size);
    return;
  }
  size_t written = fwrite(data, 1, size, file);
  bool closed = fclose(file) == 0; // fclose() writes out what fwrite() buffered, so it can fail too
  if (written != size || !closed) {
    this->_log_store_failure(written != size ? "write the file" : "finish writing the file", size);
    this->remove();
    return;
  }
  file = fopen(this->_etag_path.c_str(), "w");
  written = file != NULL ? fwrite(this->_downloaded_etag.data(), 1, this->_downloaded_etag.size(), file) : 0;
  closed = file != NULL && fclose(file) == 0;
  if (written != this->_downloaded_etag.size() || !closed) {
    this->_log_store_failure("write the ETag", size);
    this->remove();
    return;
  }
  ESP_LOGI("ManagerFile", "Stored %s.", this->_path.c_str());
}

uint8_t *ManagerFile::load(size_t *size, size_t expected_size) {
  *size = 0;
  if (!this->_stored_file_exists(expected_size)) {
    return nullptr;
  }
  struct stat st;
  stat(this->_path.c_str(), &st);
  uint8_t *data = static_cast<uint8_t *>(heap_caps_malloc(st.st_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (data == nullptr) {
    ESP_LOGE("ManagerFile", "Not enough PSRAM for the stored %s.", this->_path.c_str());
    return nullptr;
  }
  FILE *file = fopen(this->_path.c_str(), "rb");
  size_t read = file != NULL ? fread(data, 1, st.st_size, file) : 0;
  if (file != NULL) {
    fclose(file);
  }
  if (read != (size_t)st.st_size) {
    ESP_LOGE("ManagerFile", "Failed to read the stored %s.", this->_path.c_str());
    heap_caps_free(data);
    return nullptr;
  }
  *size = read;
  return data;
}

void ManagerFile::remove() {
  ::remove(this->_etag_path.c_str());
  ::remove(this->_path.c_str());
}

void ManagerFile::_log_store_failure(const char *step, size_t size) {
  int error = errno; // Before anything else can change it
  size_t total = 0, used = 0;
  esp_littlefs_info("spiffs", &total, &used);
  ESP_LOGW("ManagerFile", "Failed to store %s in LittleFS: could not %s (%s). LittleFS uses %u of %u bytes, the file needs %u. "
           "It will be downloaded again after a restart.",
           this->_path.c_str(), step, strerror(error), (unsigned)used, (unsigned)total, (unsigned)size);
}

void ManagerFile::register_updater(void (*update)()) {
  ManagerFile::_updaters.push_back(update);
  if (ManagerFile::_task_handle == NULL &&
      xTaskCreatePinnedToCore(ManagerFile::_task, "manager_files", 6144, NULL, 1, &ManagerFile::_task_handle, 1) != pdPASS) {
    ESP_LOGE("ManagerFile", "Failed to start the task that downloads files from the manager.");
    ManagerFile::_task_handle = NULL;
  }
}

void ManagerFile::request_updates() {
  if (ManagerFile::_task_handle != NULL) {
    xTaskNotifyGive(ManagerFile::_task_handle);
  }
}

void ManagerFile::_task(void *param) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (auto update : ManagerFile::_updaters) {
      update();
    }
  }
}
