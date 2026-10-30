#include <AlbumArt.hpp>
#include <ConfigManager.hpp>
#include <Nextion.hpp>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <string.h>

void AlbumArt::render(const std::string &url, uint16_t x, uint16_t y) {
  esp_log_level_set("AlbumArt", ConfigManager::log_level);

  if (url.empty()) [[unlikely]] {
    ESP_LOGD("AlbumArt", "No album art URL, nothing to render.");
    return;
  }

  std::lock_guard<std::mutex> lock_guard(AlbumArt::_request_mutex);
  // The new art covers the old, so there is nothing to clear.
  AlbumArt::_clear_when_stopped = false;
  if (AlbumArt::_rendering) {
    // Only the newest art matters, for example after skipping a track. Replace any request already
    // waiting, and stop the running render so its task starts this one when it finishes.
    ESP_LOGD("AlbumArt", "A render is already in progress, will start this one when it stops.");
    AlbumArt::_next_request = _request_t{.url = url, .x = x, .y = y};
    AlbumArt::_cancelled = true;
    return;
  }

  AlbumArt::_rendering = true;
  AlbumArt::_start_render(url, x, y);
}

void AlbumArt::cancel() {
  std::lock_guard<std::mutex> lock_guard(AlbumArt::_request_mutex);
  AlbumArt::_next_request.reset();
  AlbumArt::_clear_when_stopped = false;
  if (AlbumArt::_rendering) {
    AlbumArt::_cancelled = true;
  }
}

void AlbumArt::clear() {
  {
    std::lock_guard<std::mutex> lock_guard(AlbumArt::_request_mutex);
    AlbumArt::_next_request.reset();
    if (AlbumArt::_rendering) {
      AlbumArt::_cancelled = true;
      AlbumArt::_clear_when_stopped = true;
      return;
    }
  }
  AlbumArt::_clear_display();
}

void AlbumArt::_clear_display() {
  ESP_LOGD("AlbumArt", "Clearing the album art.");
  // Sent after every fill, so the display redraws the page only once it has drawn them.
  if (Nextion::send_raw_command("ref 0", 250) != ESP_OK) [[unlikely]] {
    ESP_LOGW("AlbumArt", "Failed to redraw the page to clear the album art.");
  }
}

void AlbumArt::_start_render(const std::string &url, uint16_t x, uint16_t y) {
  AlbumArt::_cancelled = false;
  AlbumArt::_draw_origin_x = x;
  AlbumArt::_draw_origin_y = y;

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
  AlbumArt::_fills_since_sync = 0;

  // The image currently on screen, which is exactly the last pass that was drawn. Held so the
  // next pass can emit only what changed. NULL for the first pass, which draws everything.
  uint8_t *previous = NULL;
  uint16_t previous_grid = 0;

  const uint16_t final_grid = AlbumArt::passes[sizeof(AlbumArt::passes) / sizeof(AlbumArt::passes[0]) - 1];

  for (uint16_t grid : AlbumArt::passes) {
    if (AlbumArt::_cancelled) {
      ESP_LOGD("AlbumArt", "Render cancelled before the %ux%u pass.", grid, grid);
      break;
    }

    // The last pass has no successor to difference against, so it is streamed and discarded.
    uint8_t *keep = NULL;
    if (grid != final_grid) {
      keep = (uint8_t *)malloc((size_t)grid * grid * 2);
      if (keep == NULL) [[unlikely]] {
        // Not fatal: without it the next pass simply redraws every run.
        ESP_LOGW("AlbumArt", "Could not hold the %ux%u pass, the next one will redraw in full.", grid, grid);
      }
    }

    esp_err_t err = AlbumArt::_run_pass(*base_url, grid, previous, previous_grid, keep);

    free(previous);
    previous = NULL;
    previous_grid = 0;

    if (err != ESP_OK) {
      ESP_LOGW("AlbumArt", "The %ux%u pass did not complete, giving up on the rest.", grid, grid);
      free(keep);
      break;
    }

    previous = keep;
    previous_grid = (keep != NULL) ? grid : 0;
  }

  free(previous);
  delete base_url;
  bool clear_display = false;
  {
    std::lock_guard<std::mutex> lock_guard(AlbumArt::_request_mutex);
    if (AlbumArt::_next_request.has_value()) {
      // render() was asked for newer art while this one ran. Start it, still marked as rendering.
      _request_t next = std::move(AlbumArt::_next_request.value());
      AlbumArt::_next_request.reset();
      AlbumArt::_start_render(next.url, next.x, next.y);
    } else {
      clear_display = AlbumArt::_clear_when_stopped;
      AlbumArt::_clear_when_stopped = false;
      AlbumArt::_cancelled = false;
      AlbumArt::_rendering = false;
    }
  }
  if (clear_display) {
    AlbumArt::_clear_display();
  }
  vTaskDelete(NULL);
}

esp_err_t AlbumArt::_run_pass(const std::string &base_url, uint16_t grid, const uint8_t *previous, uint16_t previous_grid, uint8_t *keep) {
  const size_t row_bytes = (size_t)grid * 2;
  const size_t expected_size = row_bytes * grid;

  // The URL from the manager already carries ?v=<hash>, so size parameters are appended with &.
  std::string url = base_url;
  url.append(url.find('?') == std::string::npos ? "?w=" : "&w=");
  url.append(std::to_string(grid));
  url.append("&h=");
  url.append(std::to_string(grid));

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

  int64_t started = esp_timer_get_time();
  uint8_t row[AlbumArt::max_row_bytes];
  uint32_t fills = 0;
  err = ESP_OK;

  for (uint16_t row_index = 0; row_index < grid; row_index++) {
    if (AlbumArt::_cancelled) {
      ESP_LOGD("AlbumArt", "Render cancelled during the %ux%u pass.", grid, grid);
      err = ESP_ERR_INVALID_STATE;
      break;
    }

    err = AlbumArt::_read_exact(client, row, row_bytes);
    if (err != ESP_OK) {
      ESP_LOGE("AlbumArt", "Read failed on row %u of the %ux%u pass.", row_index, grid, grid);
      break;
    }

    if (keep != NULL) {
      memcpy(keep + (size_t)row_index * row_bytes, row, row_bytes);
    }

    // Colour quantization would go here, masking low bits off each channel before runs are
    // found: neighbouring pixels differ by a bit or two far more often than they differ
    // visibly, which is what holds the average run down to 1.66 px.

    fills += AlbumArt::_draw_row(row, grid, row_index, previous, previous_grid);
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (err == ESP_OK) {
    uint32_t blocks = (uint32_t)grid * grid;
    ESP_LOGD("AlbumArt", "Drew %ux%u pass in %lld ms: %lu fills for %lu blocks (%lu%%).",
             grid, grid, (esp_timer_get_time() - started) / 1000, fills, blocks, fills * 100 / blocks);
  }
  return err;
}

void AlbumArt::_wait_for_display() {
  AlbumArt::_fills_since_sync = 0;
  int32_t page;
  if (Nextion::get_integer_value("dp", &page, 500, 250) != ESP_OK) {
    // Most likely the display dropped the request itself. Carry on rather than stall the render.
    ESP_LOGD("AlbumArt", "No answer from the display while pacing the render, carrying on.");
  }
}

uint32_t AlbumArt::_draw_row(const uint8_t *row, uint16_t grid, uint16_t row_index, const uint8_t *previous, uint16_t previous_grid) {
  const uint16_t block_size = AlbumArt::draw_size / grid;
  const uint32_t y = AlbumArt::_draw_origin_y + (uint32_t)row_index * block_size;

  // The pass grids do not all divide each other (80 into 32, say), so map by proportion rather
  // than by a scale factor.
  const uint16_t previous_row = (previous != NULL) ? (uint16_t)((uint32_t)row_index * previous_grid / grid) : 0;

  // Runs merge horizontally only, so every fill is one block tall. Rows repeat too, and a taller
  // fill costs no more bytes than a short one, so merging vertically as well would cut the fills further.
  uint32_t fills = 0;
  uint16_t run_start = 0;
  uint16_t run_color = AlbumArt::_pixel_at(row, grid, 0, 0);
  // A run is emitted only if some block in it differs from what is already on screen. Blocks
  // that match are still swallowed by a run around them, which is free and keeps runs long.
  // The first pass has nothing on screen to compare against, so every run is drawn. Column 0
  // is folded into the loop below, which starts by extending this run.
  bool run_changed = (previous == NULL);

  for (uint16_t col = 0; col <= grid; col++) {
    uint16_t color = 0;
    bool same = false;
    if (col < grid) {
      color = AlbumArt::_pixel_at(row, grid, 0, col);
      same = (color == run_color);
    }

    if (same) {
      if (previous != NULL && !run_changed) {
        uint16_t previous_col = (uint16_t)((uint32_t)col * previous_grid / grid);
        run_changed = (color != AlbumArt::_pixel_at(previous, previous_grid, previous_row, previous_col));
      }
      continue;
    }

    if (run_changed) {
      if (Nextion::fill(AlbumArt::_draw_origin_x + (uint32_t)run_start * block_size, y,
                        (uint32_t)(col - run_start) * block_size, block_size, run_color, 250) != ESP_OK) {
        // The display is not in a state to be drawn on -- updating, most likely. Stop rather
        // than spin through thousands of failing writes.
        ESP_LOGW("AlbumArt", "Display rejected a fill, cancelling the render.");
        AlbumArt::_cancelled = true;
        return fills;
      }
      fills++;
      if (++AlbumArt::_fills_since_sync >= AlbumArt::_fills_per_sync) {
        AlbumArt::_wait_for_display();
      }
    }

    if (col == grid) {
      break;
    }

    run_start = col;
    run_color = color;
    if (previous != NULL) {
      uint16_t previous_col = (uint16_t)((uint32_t)col * previous_grid / grid);
      run_changed = (color != AlbumArt::_pixel_at(previous, previous_grid, previous_row, previous_col));
    } else {
      run_changed = true;
    }
  }

  return fills;
}

esp_err_t AlbumArt::_read_exact(esp_http_client_handle_t client, uint8_t *buffer, size_t size) {
  size_t read_total = 0;
  while (read_total < size) {
    int read_now = esp_http_client_read(client, (char *)buffer + read_total, size - read_total);
    if (read_now <= 0) {
      return ESP_FAIL;
    }
    read_total += read_now;
  }
  return ESP_OK;
}
