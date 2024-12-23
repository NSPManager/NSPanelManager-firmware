#include <ConfigManager.hpp>
#include <LittleFS.hpp>
#include <MqttManager.hpp>
#include <NSPM_ConfigManager.hpp>
#include <Nextion.hpp>
#include <Nextion_event.hpp>
#include <UpdateManager.hpp>
#include <UpdateManager_event.hpp>
#include <cJSON.h>
#include <cmath>
#include <esp_https_ota.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>

ESP_EVENT_DEFINE_BASE(UPDATEMANAGER_EVENT);

void UpdateManager::init() {
  esp_log_level_set("UpdateManager", esp_log_level_t::ESP_LOG_DEBUG); // TODO: Read from config
  UpdateManager::_download_data_store_mutex = xSemaphoreCreateMutex();

  MqttManager::register_handler(MQTT_EVENT_DATA, UpdateManager::_mqtt_event_handler, NULL);

  std::string command_topic = "nspanel/mqttmanager_";
  command_topic.append(NSPM_ConfigManager::get_manager_address());
  command_topic.append("/nspanel/");
  command_topic.append(ConfigManager::wifi_hostname);
  command_topic.append("/command");
  MqttManager::subscribe(command_topic);
}

void UpdateManager::update_gui(void *param) {
  NSPanelConfig config;
  if (NSPM_ConfigManager::get_config(&config) == ESP_OK) {
    esp_event_handler_register(NEXTION_EVENT, ESP_EVENT_ANY_ID, UpdateManager::_nextion_event_handler, NULL);

    std::string tft_file_url = "http://";
    tft_file_url.append(NSPM_ConfigManager::get_manager_address());
    tft_file_url.append(":");
    tft_file_url.append(std::to_string(NSPM_ConfigManager::get_manager_port()));
    tft_file_url.append(config.is_us_panel ? "/download_tft_us" : "/download_tft_eu");

    size_t remote_tft_file_size;
    while (UpdateManager::_get_remote_file_size(tft_file_url, &remote_tft_file_size) != ESP_OK) {
      ESP_LOGW("UpdateManager", "Failed to get remote TFT file size. Will try again in 500ms");
      vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI("UpdateManager", "Will start updating GUI. Remote file size: %zu bytes", remote_tft_file_size);
    while (Nextion::start_update(ConfigManager::nextion_upload_baudrate, ConfigManager::use_latest_nextion_upload_protocol, remote_tft_file_size) != ESP_OK) {
      ESP_LOGW("UpdateManager", "Failed to init update process with Nextion display. Will try again in 5000ms");
      vTaskDelay(pdMS_TO_TICKS(5000));
    }

    esp_event_post(UPDATEMANAGER_EVENT, updatemanager_event_t::NEXTION_UPDATE_STARTED, NULL, 0, pdMS_TO_TICKS(500));

    // Update started. Wait for notifications and when they arrive, download data from offset into buffer and write to display.
    std::vector<uint8_t> tft_data_buffer;
    float progress;
    for (;;) {
      if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20000)) == pdPASS) {
        // Got event to send new data. Calculate next chunk size but a max of 4096 bytes:
        UpdateManager::_nextion_update_last_written_bytes = remote_tft_file_size - UpdateManager::_nextion_update_current_offset;
        if (UpdateManager::_nextion_update_last_written_bytes > 4096) {
          UpdateManager::_nextion_update_last_written_bytes = 4096;
        }

        ESP_LOGD("UpdateManager", "Received notification. Will download TFT bytes %llu and %llu bytes forward.", UpdateManager::_nextion_update_current_offset, UpdateManager::_nextion_update_last_written_bytes);
        esp_err_t res;
        for (;;) { // Try downloading data until it succeeds
          res = UpdateManager::_download_data(&tft_data_buffer, tft_file_url.c_str(), UpdateManager::_nextion_update_current_offset, UpdateManager::_nextion_update_last_written_bytes);
          if (res == ESP_OK && tft_data_buffer.size() == UpdateManager::_nextion_update_last_written_bytes) {
            break; // Download was successful. Exit loop.
          } else {
            tft_data_buffer.clear(); // Clear buffer and try again.
            ESP_LOGE("UpdateManager", "Failed to download TFT chunk when updating GUI. Will try again in 5 seconds.");
            vTaskDelay(pdMS_TO_TICKS(5000));
          }
        }

        while (Nextion::write_update_bytes(tft_data_buffer.data(), tft_data_buffer.size()) != ESP_OK) {
          ESP_LOGE("UpdateManager", "Failed to write TFT chunk to UART while updating GUI. Will try again in 5 seconds.");
          tft_data_buffer.clear(); // Failed to download data. Clear buffer and try again.
          vTaskDelay(pdMS_TO_TICKS(5000));
        }
        ESP_LOGD("UpdateManager", "Wrote %zu bytes to Nextion display. Total bytes written: %llu", tft_data_buffer.size(), UpdateManager::_nextion_update_current_offset + tft_data_buffer.size());

        // Clear buffer for next use
        tft_data_buffer.clear();

        progress = (std::round((((double)UpdateManager::_nextion_update_current_offset + (double)UpdateManager::_nextion_update_last_written_bytes) / (double)remote_tft_file_size) * 100) / 100) * 100;
        esp_event_post(UPDATEMANAGER_EVENT, updatemanager_event_t::NEXTION_UPDATE_PROGRESS, &progress, sizeof(progress), pdMS_TO_TICKS(250));

        if (UpdateManager::_nextion_update_current_offset + UpdateManager::_nextion_update_last_written_bytes >= remote_tft_file_size) {
          break; // Update complete
        }
      } else {
        ESP_LOGE("UpdateManager", "Have still not received a new Nextion update event while waiting for 20s. Will wait another 20s.");
      }
    }

    esp_event_post(UPDATEMANAGER_EVENT, updatemanager_event_t::NEXTION_UPDATE_FINISHED, NULL, 0, pdMS_TO_TICKS(500));
    ESP_LOGI("UpdateManager", "Nextion GUI update complete. Will restart in 10 seconds.");
    vTaskDelay(pdMS_TO_TICKS(10000));

    esp_restart();
    vTaskDelete(NULL);
  } else {
    ESP_LOGE("UpdateManager", "Failed to get NSPanelConfig when trying to update GUI.");
  }
}

void UpdateManager::update_firmware(void *param) {
  std::string firmware_md5_string = "http://";
  firmware_md5_string.append(NSPM_ConfigManager::get_manager_address());
  firmware_md5_string.append(":");
  firmware_md5_string.append(std::to_string(NSPM_ConfigManager::get_manager_port()));
  firmware_md5_string.append("/checksum_firmware");

  std::vector<uint8_t> data;
  if (UpdateManager::_download_data(&data, firmware_md5_string.c_str(), -1, -1) == ESP_OK) {
    std::string md5_string = std::string((char *)data.data(), data.size());
    ESP_LOGD("UpdateManager", "Got new MD5 sum from manager: %s", md5_string.c_str());

    if (md5_string.compare(ConfigManager::md5_firmware) != 0) {
      ESP_LOGI("UpdateManager", "New firmware available. Will update OTA.");
      if (UpdateManager::_update_firmware_ota() == ESP_OK) {
        ESP_LOGI("UpdateManager", "Firmware update complete, will save new MD5 checksum.");
        ConfigManager::md5_firmware = md5_string;
        ConfigManager::save_config();

        UpdateManager::update_littlefs(NULL);

        ESP_LOGI("UpdateManager", "Update complete. Will start in 2 seconds");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
      }
    } else {
      ESP_LOGI("UpdateManager", "Firmware already up to date. Will check LittleFS.");
      UpdateManager::update_littlefs(NULL);
    }
  } else {
    ESP_LOGE("UpdateManager", "Failed to new firmware MD5 checksum.");
  }

  UpdateManager::_current_update_task = NULL;
  vTaskDelete(NULL);
}

void UpdateManager::update_littlefs(void *param) {
  // TODO: Implement
  std::string littlefs_md5_string = "http://";
  littlefs_md5_string.append(NSPM_ConfigManager::get_manager_address());
  littlefs_md5_string.append(":");
  littlefs_md5_string.append(std::to_string(NSPM_ConfigManager::get_manager_port()));
  littlefs_md5_string.append("/checksum_data_file");

  std::vector<uint8_t> data;
  if (UpdateManager::_download_data(&data, littlefs_md5_string.c_str(), -1, -1) == ESP_OK) {
    std::string md5_string = std::string((char *)data.data(), data.size());
    ESP_LOGD("UpdateManager", "Got new MD5 sum from manager: %s", md5_string.c_str());

    if (md5_string.compare(ConfigManager::md5_data_file) != 0) {
      ESP_LOGI("UpdateManager", "New LittleFS available. Will update OTA.");
      LittleFS::unmount(); // Unmount LittleFS
      if (UpdateManager::_update_littlefs_ota() == ESP_OK) {
        ESP_LOGI("UpdateManager", "LittleFS update complete, will save new MD5 checksum.");
        ConfigManager::md5_data_file = md5_string;
        // Save the existing config loaded into memory into the new LittleFS partition.
        LittleFS::mount();
        ConfigManager::save_config();

        ESP_LOGI("UpdateManager", "Update complete. Will start in 2 seconds");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
      }
    } else {
      ESP_LOGI("UpdateManager", "LittleFS already up to date.");
    }
  } else {
    ESP_LOGE("UpdateManager", "Failed to new LittleFS MD5 checksum.");
  }
}

esp_err_t UpdateManager::_download_data(std::vector<uint8_t> *return_data, const char *download_url, int64_t offset, int64_t length) {
  if (xSemaphoreTake(UpdateManager::_download_data_store_mutex, portMAX_DELAY) == pdPASS) {
    UpdateManager::_download_data_store = return_data;

    esp_http_client_config_t config = {
        .url = download_url,
        .event_handler = UpdateManager::_http_event_handler,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (offset >= 0 && length > 0) {
      std::string range_header = "bytes=";
      range_header.append(std::to_string(offset));
      range_header.append("-");
      range_header.append(std::to_string(offset + length));
      esp_err_t err = esp_http_client_set_header(client, "Range", range_header.c_str());
      if (err != ESP_OK) {
        ESP_LOGE("UpdateManager", "Failed to set range header will trying to download chunk of data! Error: %s", esp_err_to_name(err));
        return ESP_ERR_NOT_FINISHED;
      }
    }

    // Perform the actual HTTP request to get data
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
      esp_http_client_cleanup(client);
      UpdateManager::_download_data_store = nullptr;
      xSemaphoreGive(UpdateManager::_download_data_store_mutex);
      return ESP_OK;
    } else {
      ESP_LOGE("UpdateManager", "Failed to download data from %s. Got error: %s. HTTP Status code: %d.", download_url, esp_err_to_name(err), esp_http_client_get_status_code(client));
      esp_http_client_cleanup(client);
      UpdateManager::_download_data_store = nullptr;
      xSemaphoreGive(UpdateManager::_download_data_store_mutex);
      return ESP_ERR_NOT_FINISHED;
    }
  }
  return ESP_ERR_NOT_FINISHED;
}

esp_err_t UpdateManager::_get_remote_file_size(std::string url, size_t *file_size) {
  // Start actual littlefs update
  esp_http_client_config_t http_config = {
      .url = url.c_str(),
      .cert_pem = NULL,
      .keep_alive_enable = true,
  };

  // Setup HTTP connection:
  esp_http_client_handle_t client = esp_http_client_init(&http_config);
  if (client == NULL) {
    ESP_LOGE("UpdateManager", "Failed to initialise HTTP connection");
    return ESP_ERR_NOT_FINISHED;
  }
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE("UpdateManager", "Failed to open HTTP connection: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return ESP_ERR_NOT_FINISHED;
  }

  esp_http_client_fetch_headers(client);
  *file_size = esp_http_client_get_content_length(client);

  esp_http_client_cleanup(client);
  return ESP_OK;
}

esp_err_t UpdateManager::_http_event_handler(esp_http_client_event_t *event) {
  switch (event->event_id) {
  case HTTP_EVENT_ON_DATA:
    if (!esp_http_client_is_chunked_response(event->client) && UpdateManager::_download_data_store != nullptr) {
      UpdateManager::_download_data_store->reserve(UpdateManager::_download_data_store->size() + event->data_len);
      UpdateManager::_download_data_store->insert(UpdateManager::_download_data_store->end(), (uint8_t *)event->data, (uint8_t *)event->data + event->data_len);
    } else {
      ESP_LOGE("UpdateManager", "Download data is chunked. Not supported!");
    }
    break;

  default:
    break;
  }
  return ESP_OK;
}

void UpdateManager::_mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  // TODO: Move all "command topic listening" to a separate "CommandManager" or something like that.

  // We don't check what type of event it is because we only register to "ON DATA"-events
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
  std::string topic_string = std::string(event->topic, event->topic_len);

  std::string command_topic = "nspanel/mqttmanager_";
  command_topic.append(NSPM_ConfigManager::get_manager_address());
  command_topic.append("/nspanel/");
  command_topic.append(ConfigManager::wifi_hostname);
  command_topic.append("/command");

  if (topic_string.compare(command_topic) == 0) {
    cJSON *json = cJSON_ParseWithLength(event->data, event->data_len);
    if (json == NULL) {
      ESP_LOGW("UpgradeManager", "Failed to parse payload as JSON.");
      // Failed to parse as JSON
      return;
    }

    cJSON *item = cJSON_GetObjectItem(json, "command");
    if (cJSON_IsString(item) && item->valuestring != NULL) {
      std::string command_string = item->valuestring;

      if (command_string.compare("reboot") == 0) { // TODO: Move to some place more fitting.
        ESP_LOGI("UpgradeManager", "Received command to reboot. Will reboot NSPanel.");
        esp_restart();
      } else if (command_string.compare("firmware_update") == 0 && UpdateManager::_current_update_task == NULL) {
        xTaskCreatePinnedToCore(UpdateManager::update_firmware, "update_firmware", 8192, NULL, 4, &UpdateManager::_current_update_task, 1);
      } else if (command_string.compare("tft_update") == 0 && UpdateManager::_current_update_task == NULL) {
        xTaskCreatePinnedToCore(UpdateManager::update_gui, "update_firmware", 8192, NULL, 4, &UpdateManager::_current_update_task, 1);
      } else {
        ESP_LOGW("UpgradeManager", "Unknown command: %s", item->valuestring);
      }
    }

    cJSON_free(json);
  }
}

void UpdateManager::_nextion_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  switch (event_id) {
  case nextion_event_t::UPDATE_READY_FOR_NEXT_CHUNK: {
    UpdateManager::_nextion_update_current_offset += UpdateManager::_nextion_update_last_written_bytes;
    xTaskNotifyGive(UpdateManager::_current_update_task);
    break;
  }

  case nextion_event_t::UPDATE_JUMP_TO_OFFSET: {
    size_t *jump_offset = (size_t *)event_data;
    if (*jump_offset > 0) { // If set to zero, continue and treat it as a 0x05 as 0 means "continue and do not jump"
      UpdateManager::_nextion_update_current_offset = *jump_offset;
    } else {
      UpdateManager::_nextion_update_current_offset += UpdateManager::_nextion_update_last_written_bytes;
    }
    ESP_LOGI("UpdateManager", "Received instruction from Nextion display to jump to %zu TFT file offset.", *jump_offset);
    xTaskNotifyGive(UpdateManager::_current_update_task);
    break;
  }

  default:
    break;
  }
}

esp_err_t UpdateManager::_update_firmware_ota() {
  // Firmware download URL
  std::string firmware_download_url = "http://";
  firmware_download_url.append(NSPM_ConfigManager::get_manager_address());
  firmware_download_url.append(":");
  firmware_download_url.append(std::to_string(NSPM_ConfigManager::get_manager_port()));
  firmware_download_url.append("/download_firmware");

  // Send event that we actually started with firmware update
  esp_event_post(UPDATEMANAGER_EVENT, updatemanager_event_t::FIRMWARE_UPDATE_STARTED, NULL, 0, pdMS_TO_TICKS(500));

  // Start actual firmware update
  esp_http_client_config_t http_config = {
      .url = firmware_download_url.c_str(),
      .cert_pem = NULL,
      .keep_alive_enable = true,
  };

  esp_err_t ret;
  const esp_https_ota_config_t config = {
      .http_config = &http_config,
      // .partial_http_download = true,
      // .max_http_request_size = 16384,
  };

  esp_https_ota_handle_t https_ota_handle = 0;
  ret = esp_https_ota_begin(&config, &https_ota_handle);
  if (ret != ESP_OK) {
    ESP_LOGE("UpdateManager", "esp_https_ota_begin failed: %s", esp_err_to_name(ret));
    return ESP_ERR_NOT_FINISHED;
  }

  esp_app_desc_t app_desc;
  ret = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
  if (ret != ESP_OK) {
    ESP_LOGE("UpdateManager", "esp_https_ota_get_img_desc failed");
    return ESP_ERR_NOT_FINISHED;
  }

  // ret = UpdateManager::_validate_image_header(&app_desc);
  // if (ret != ESP_OK) {
  //   ESP_LOGE("UpdateManager", "image header verification failed");
  //   return ESP_ERR_NOT_FINISHED;
  // }

  int firmware_size = esp_https_ota_get_image_size(https_ota_handle);
  ESP_LOGI("UpdateManager", "Will update firmware from %s. Remote file size: %d bytes.", firmware_download_url.c_str(), firmware_size);

  int data_read_len = 0;
  float progress = 0;
  uint64_t last_progress_update = 0;
  while (true) {
    ret = esp_https_ota_perform(https_ota_handle);
    if (ret == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
      if (esp_timer_get_time() / 1000 - last_progress_update > 250) {
        // 250ms has passed since last check, update progress percentage.
        data_read_len = esp_https_ota_get_image_len_read(https_ota_handle);
        // Calculate progress to within 1 decimals
        progress = (std::round(((double)data_read_len / (double)firmware_size) * 100) / 100) * 100;
        esp_event_post(UPDATEMANAGER_EVENT, updatemanager_event_t::FIRMWARE_UPDATE_PROGRESS, &progress, sizeof(progress), pdMS_TO_TICKS(50));

        // ESP_LOGD("UpdateManager", "Progress %d/%d (%f%%)", data_read_len, firmware_size, progress);
        last_progress_update = esp_timer_get_time() / 1000;
      }
    } else if (ret == ESP_OK) {
      progress = 100;
      break;
    } else {
      break;
    }
  }

  // Finish the OTA update
  if (ret == ESP_OK) {
    ret = esp_https_ota_finish(https_ota_handle);
    esp_event_post(UPDATEMANAGER_EVENT, updatemanager_event_t::FIRMWARE_UPDATE_FINISHED, NULL, 0, pdMS_TO_TICKS(500));
    if (ret == ESP_OK) {
      ESP_LOGI("UpdateManager", "Firmware OTA update successful.");
      return ESP_OK;
    } else {
      ESP_LOGI("UpdateManager", "Firmware OTA update failed: %s", esp_err_to_name(ret));
      return ESP_ERR_NOT_FINISHED;
    }
  } else {
    esp_event_post(UPDATEMANAGER_EVENT, updatemanager_event_t::FIRMWARE_UPDATE_FINISHED, NULL, 0, pdMS_TO_TICKS(500));
    ESP_LOGI("UpdateManager", "Firmware OTA update failed: %s", esp_err_to_name(ret));
    return ESP_ERR_NOT_FINISHED;
  }
}

esp_err_t UpdateManager::_update_littlefs_ota() {
  // Firmware download URL
  std::string littlefs_download_url = "http://";
  littlefs_download_url.append(NSPM_ConfigManager::get_manager_address());
  littlefs_download_url.append(":");
  littlefs_download_url.append(std::to_string(NSPM_ConfigManager::get_manager_port()));
  littlefs_download_url.append("/download_data_file");

  // Send event that we actually started with littlefs update
  esp_event_post(UPDATEMANAGER_EVENT, updatemanager_event_t::LITTLEFS_UPDATE_STARTED, NULL, 0, pdMS_TO_TICKS(500));

  // Start actual littlefs update
  esp_http_client_config_t http_config = {
      .url = littlefs_download_url.c_str(),
      .cert_pem = NULL,
      .keep_alive_enable = true,
  };

  // Find LittleFS partition
  esp_partition_t *littlefs_partition = NULL;
  esp_partition_iterator_t littlefs_partition_iterator = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
  while (littlefs_partition_iterator != NULL) {
    littlefs_partition = (esp_partition_t *)esp_partition_get(littlefs_partition_iterator);
    littlefs_partition_iterator = esp_partition_next(littlefs_partition_iterator);
  }
  vTaskDelay(pdMS_TO_TICKS(500));
  esp_partition_iterator_release(littlefs_partition_iterator);

  // Check that we found the partition
  if (littlefs_partition == NULL) {
    ESP_LOGE("UpdateManager", "Failed to find LittleFS partition! Will cancel update process.");
    return ESP_ERR_NOT_FINISHED;
  } else {
    ESP_LOGI("UpdateManager", "Found LittleFS partition:");
    ESP_LOGI("UpdateManager", "LittelFS: partition type = %d.", littlefs_partition->type);
    ESP_LOGI("UpdateManager", "LittelFS: partition subtype = %d.", littlefs_partition->subtype);
    ESP_LOGI("UpdateManager", "LittelFS: partition starting address = %ld.", littlefs_partition->address);
    ESP_LOGI("UpdateManager", "LittelFS: partition size = %ld.", littlefs_partition->size);
    ESP_LOGI("UpdateManager", "LittelFS: partition label = %s.", littlefs_partition->label);
    ESP_LOGI("UpdateManager", "LittelFS: partition subtype = %d.", littlefs_partition->encrypted);
    ESP_LOGI("UpdateManager", "---");
  }

  // Setup HTTP connection:
  esp_http_client_handle_t client = esp_http_client_init(&http_config);
  if (client == NULL) {
    ESP_LOGE("UpdateManager", "Failed to initialise HTTP connection");
    return ESP_ERR_NOT_FINISHED;
  }
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE("UpdateManager", "Failed to open HTTP connection: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return ESP_ERR_NOT_FINISHED;
  }

  esp_http_client_fetch_headers(client);
  uint64_t total_file_size = esp_http_client_get_content_length(client);
  ESP_LOGI("UpdateManager", "Remote LittleFS image size: %lld bytes.", total_file_size);

  // Delete current LittleFS partition
  err = esp_partition_erase_range(littlefs_partition, 0, littlefs_partition->size);
  if (err != ESP_OK) {
    ESP_LOGE("UpdateManager", "Failed to erase existing LittleFS partition. Error: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return ESP_ERR_NOT_FINISHED;
  }

  uint64_t last_progress_update = 0;
  float progress;

  uint64_t total_bytes_read = 0;
  constexpr uint16_t buffer_size = 4096;
  char data_buffer[buffer_size];
  for (;;) {
    int actual_data_read = esp_http_client_read(client, data_buffer, buffer_size);

    if (actual_data_read < 0) {
      ESP_LOGE("UpdateManager", "HTTP connection to manager failed! Will cancel operation.");
      esp_http_client_cleanup(client);
      return ESP_ERR_NOT_FINISHED;
    } else if (actual_data_read > 0) {
      err = esp_partition_write(littlefs_partition, total_bytes_read, (const void *)data_buffer, actual_data_read);
      if (err != ESP_OK) {
        ESP_LOGE("UpdateManager", "Failed to write data to buffer. Will cancel LittleFS update!");
        esp_http_client_cleanup(client);
        return ESP_ERR_NOT_FINISHED;
      } else {
        total_bytes_read += actual_data_read;
      }

      if (esp_timer_get_time() / 1000 - last_progress_update > 250) {
        // 250ms has passed since last check, update progress percentage.        // Calculate progress to within 1 decimals
        progress = (std::round(((double)total_bytes_read / (double)total_file_size) * 100) / 100) * 100;
        esp_event_post(UPDATEMANAGER_EVENT, updatemanager_event_t::LITTLEFS_UPDATE_PROGRESS, &progress, sizeof(progress), pdMS_TO_TICKS(50));

        // ESP_LOGD("UpdateManager", "Progress %llu/%llu (%f%%)", total_bytes_read, total_file_size, progress);
        last_progress_update = esp_timer_get_time() / 1000;
      }
    } else {
      // Received 0 bytes ie. we've read all data and connection closed.
      ESP_LOGI("UpdateManager", "Wrote %llu bytes to LittleFS partition via OTA from manager. Update complete.", total_bytes_read);
      esp_http_client_cleanup(client);
      esp_event_post(UPDATEMANAGER_EVENT, updatemanager_event_t::LITTLEFS_UPDATE_FINISHED, NULL, 0, pdMS_TO_TICKS(50));
      return ESP_OK;
    }
  }

  return ESP_ERR_NOT_FINISHED;
}