#include <AlbumArt.hpp>
#include <ConfigManager.hpp>
#include <Nextion.hpp>
#include <esp_http_client.h>
#include <esp_log.h>

void AlbumArt::render(const std::string &url) {
  esp_log_level_set("AlbumArt", ConfigManager::log_level);

  if (url.empty()) [[unlikely]] {
    ESP_LOGD("AlbumArt", "No album art URL, nothing to render.");
    return;
  }

  bool expected = false;
  if (!AlbumArt::_rendering.compare_exchange_strong(expected, true)) {
    ESP_LOGD("AlbumArt", "A render is already in progress, ignoring this one.");
    return;
  }

  // The task owns this and deletes it when done.
  std::string *url_copy = new std::string(url);
  if (xTaskCreate(&AlbumArt::_task_render, "album_art", 4096, url_copy, 2, NULL) != pdPASS) [[unlikely]] {
    ESP_LOGE("AlbumArt", "Failed to spawn album art render task.");
    delete url_copy;
    AlbumArt::_rendering = false;
  }
}

void AlbumArt::_task_render(void *param) {
  std::string *base_url = (std::string *)param;

  // One buffer, sized for the finest pass, reused by all of them.
  uint8_t *buffer = (uint8_t *)malloc(AlbumArt::max_buffer_size);
  if (buffer == NULL) [[unlikely]] {
    ESP_LOGE("AlbumArt", "Failed to allocate %zu bytes for album art.", AlbumArt::max_buffer_size);
    delete base_url;
    AlbumArt::_rendering = false;
    vTaskDelete(NULL);
    return;
  }

  for (uint16_t grid : AlbumArt::passes) {
    size_t expected_size = (size_t)grid * grid * 2;

    // The URL from the manager already carries ?v=<hash>, so size parameters are appended with &.
    std::string url = *base_url;
    url.append(url.find('?') == std::string::npos ? "?w=" : "&w=");
    url.append(std::to_string(grid));
    url.append("&h=");
    url.append(std::to_string(grid));

    if (AlbumArt::_fetch(url, buffer, expected_size) != ESP_OK) {
      ESP_LOGW("AlbumArt", "Failed to fetch the %ux%u pass, giving up on the rest.", grid, grid);
      break;
    }

    AlbumArt::_draw_pass(buffer, grid);
    ESP_LOGI("AlbumArt", "Drew %ux%u pass (%u blocks).", grid, grid, grid * grid);
  }

  free(buffer);
  delete base_url;
  AlbumArt::_rendering = false;
  vTaskDelete(NULL);
}

esp_err_t AlbumArt::_fetch(const std::string &url, uint8_t *buffer, size_t expected_size) {
  esp_http_client_config_t http_client_config = {};
  http_client_config.url = url.c_str();
  http_client_config.cert_pem = NULL;
  http_client_config.timeout_ms = 5000;

  esp_http_client_handle_t client = esp_http_client_init(&http_client_config);
  if (client == NULL) [[unlikely]] {
    ESP_LOGE("AlbumArt", "Failed to init http client for %s", url.c_str());
    return ESP_FAIL;
  }

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) [[unlikely]] {
    ESP_LOGE("AlbumArt", "Failed to open %s: %s", url.c_str(), esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return err;
  }

  int64_t content_length = esp_http_client_fetch_headers(client);
  int status_code = esp_http_client_get_status_code(client);
  if (status_code != 200) {
    // 404 is the normal "this player has no album art right now" answer from the manager.
    ESP_LOGW("AlbumArt", "Got HTTP %d for %s", status_code, url.c_str());
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_FAIL;
  }

  if (content_length >= 0 && (size_t)content_length != expected_size) [[unlikely]] {
    ESP_LOGE("AlbumArt", "Expected %zu bytes of RGB565 but the manager sent %lld.", expected_size, content_length);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_FAIL;
  }

  size_t read_total = 0;
  while (read_total < expected_size) {
    int read_now = esp_http_client_read(client, (char *)buffer + read_total, expected_size - read_total);
    if (read_now <= 0) {
      ESP_LOGE("AlbumArt", "Read failed after %zu of %zu bytes.", read_total, expected_size);
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return ESP_FAIL;
    }
    read_total += read_now;
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return ESP_OK;
}

void AlbumArt::_draw_pass(const uint8_t *buffer, uint16_t grid) {
  const uint16_t block_size = AlbumArt::draw_size / grid;

  for (uint16_t row = 0; row < grid; row++) {
    for (uint16_t col = 0; col < grid; col++) {
      size_t offset = ((size_t)row * grid + col) * 2;
      // RGB565, little endian, as documented on the manager's endpoint.
      uint16_t color = (uint16_t)buffer[offset] | ((uint16_t)buffer[offset + 1] << 8);

      // fill() is fire and forget -- it writes the command and returns without waiting for the
      // display, so the cost here is purely bytes on the UART.
      Nextion::fill(AlbumArt::draw_origin_x + col * block_size,
                    AlbumArt::draw_origin_y + row * block_size,
                    block_size, block_size, color, 250);
    }
  }
}
