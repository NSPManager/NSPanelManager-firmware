#include <ConfigManager.hpp>
#include <MqttManager.hpp>
#include <NSPM_ConfigManager.hpp>
#include <UpdateManager.hpp>
#include <UpdateManager_event.hpp>
#include <cJSON.h>
#include <esp_https_ota.h>
#include <esp_log.h>

ESP_EVENT_DEFINE_BASE(UPDATEMANAGER_EVENT);

void UpdateManager::init() {
  esp_log_level_set("UpdateManager", esp_log_level_t::ESP_LOG_DEBUG); // TODO: Read from config
  ESP_LOGI("UpdateManager", "Initializing UpdateManager.");
  if (UpdateManager::_download_data_store_mutex == NULL) {
    UpdateManager::_download_data_store_mutex = xSemaphoreCreateMutex();
  }

  MqttManager::register_handler(MQTT_EVENT_DATA, UpdateManager::_mqtt_event_handler, NULL);

  std::string command_topic = "nspanel/mqttmanager_";
  command_topic.append(NSPM_ConfigManager::get_manager_address());
  command_topic.append("/nspanel/");
  command_topic.append(ConfigManager::wifi_hostname);
  command_topic.append("/command");
  MqttManager::subscribe(command_topic);

  ESP_LOGI("UpdateManager", "Initialization complete.");
}

void UpdateManager::update_gui() {
}

void UpdateManager::update_firmware() {
  std::string firmware_md5_string = "http://";
  firmware_md5_string.append(NSPM_ConfigManager::get_manager_address());
  firmware_md5_string.append(":");
  firmware_md5_string.append(std::to_string(NSPM_ConfigManager::get_manager_port()));
  firmware_md5_string.append("/checksum_firmware");

  std::vector<uint8_t> data;
  if (UpdateManager::_download_data(&data, firmware_md5_string.c_str()) == ESP_OK) {
    std::string md5_string = std::string((char *)data.data(), data.size());
    ESP_LOGD("UpdateManager", "Got new MD5 sum from manager: %s", md5_string.c_str());
  } else {
    ESP_LOGE("UpdateManager", "Failed to new firmware MD5 checksum.");
  }
}

void UpdateManager::update_littlefs() {
}

esp_err_t UpdateManager::_download_data(std::vector<uint8_t> *return_data, const char *download_url) {
  if (xSemaphoreTake(UpdateManager::_download_data_store_mutex, portMAX_DELAY) == pdPASS) {
    UpdateManager::_download_data_store = return_data;

    esp_http_client_config_t config = {
        .url = download_url,
        .event_handler = UpdateManager::_http_event_handler};

    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Perform the actual HTTP request to get data
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
      esp_http_client_cleanup(client);
      xSemaphoreGive(UpdateManager::_download_data_store);
      return ESP_OK;
    } else {
      ESP_LOGE("UpdateManager", "Failed to download data from %s. Got error: %s. HTTP Status code: %d.", download_url, esp_err_to_name(err), esp_http_client_get_status_code(client));
      esp_http_client_cleanup(client);
      xSemaphoreGive(UpdateManager::_download_data_store);
      return ESP_ERR_NOT_FINISHED;
    }
  }
  return ESP_ERR_NOT_FINISHED;
}

esp_err_t UpdateManager::_http_event_handler(esp_http_client_event_t *event) {
  switch (event->event_id) {
  case HTTP_EVENT_ERROR:
    ESP_LOGI("UpdateManager", "HTTP_EVENT_ERROR");
    break;
  case HTTP_EVENT_ON_CONNECTED:
    ESP_LOGI("UpdateManager", "HTTP_EVENT_ON_CONNECTED");
    break;
  case HTTP_EVENT_ON_DATA:
    if (!esp_http_client_is_chunked_response(event->client) && UpdateManager::_download_data_store != nullptr) {
      UpdateManager::_download_data_store->resize(UpdateManager::_download_data_store->size() + event->data_len); // Resize to handle new data size

      // Write out the data received
      memcpy(UpdateManager::_download_data_store->data() + UpdateManager::_download_data_store->size() - event->data_len, event->data, event->data_len);
    }
    break;
  case HTTP_EVENT_DISCONNECTED:
    ESP_LOGI("UpdateManager", "HTTP_EVENT_ON_DISCONNECTED");
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

      if (command_string.compare("reboot") == 0) {
        ESP_LOGI("UpgradeManager", "Received command to reboot. Will reboot NSPanel.");
        esp_restart();
      } else if (command_string.compare("firmware_update") == 0) {
        UpdateManager::update_firmware();
      } else if (command_string.compare("tft_update") == 0) {
        UpdateManager::update_gui();
      } else {
        ESP_LOGW("UpgradeManager", "Unknown command: %s", item->valuestring);
      }
    }

    cJSON_free(json);
  }
}