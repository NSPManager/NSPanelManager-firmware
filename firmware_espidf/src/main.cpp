#include <ConfigManager.hpp>
#include <InterfaceManager.hpp>
#include <LittleFS.hpp>
#include <MqttManager.hpp>
#include <NSPM_ConfigManager.hpp>
#include <NSPM_version.hpp>
#include <Nextion.hpp>
#include <RoomManager.hpp>
#include <StatusUpdateManager.hpp>
#include <UpdateManager.hpp>
#include <WebManager.hpp>
#include <WiFiManager.hpp>
#include <esp_log.h>
#include <format>
#include <nvs_flash.h>

// Topic on MQTT to send log messages to
std::string mqtt_log_topic;
TaskHandle_t task_publish_mqtt_log_message_handle = NULL;
QueueHandle_t publish_mqtt_log_messages_queue = NULL;

void task_publish_mqtt_log_message(void *param) {
  char *log_message;
  for (;;) {
    if (xQueueReceive(publish_mqtt_log_messages_queue, &log_message, portMAX_DELAY) == pdTRUE) {
      if (MqttManager::connected()) {
        MqttManager::publish(mqtt_log_topic, log_message, strlen(log_message), false);
      }
      free(log_message);
    }
  }
}

int custom_log_vprintf(const char *fmt, va_list args) {
  // vprintf(fmt, args); // Keep default behavior, print to UART

  char *buffer;
  int len = vasprintf(&buffer, fmt, args);
  if (len != -1) {
    printf(buffer);
    // Wait a maximum of 100ms to get mutex to add message to queue
    if (publish_mqtt_log_messages_queue != NULL && task_publish_mqtt_log_message_handle != NULL) {
      // TODO: Is it really necessary to have a separate task for sending logs over MQTT?
      if (xQueueSend(publish_mqtt_log_messages_queue, &buffer, pdMS_TO_TICKS(100)) != pdTRUE) {
        free(buffer);
      }
    } else {
      free(buffer);
    }
  }

  return len;
}

extern "C" void app_main() {
  // Set global log level initially. This is later set from saved config.
  // esp_log_level_set("*", ESP_LOG_DEBUG);

  ESP_LOGI("Main", "Starting NSPanel Manager firmware. Version " NSPM_VERSION ".");

  esp_event_loop_create_default();

  nvs_flash_init();
  if (LittleFS::mount() != ESP_OK) {
    ESP_LOGE("Main", "Failed to mount LittleFS!");
  }

  ConfigManager::create_default(); // Set default values on all config entities
  if (ConfigManager::load_config() != ESP_OK) {
    ESP_LOGE("Main", "Failed to load config from LittleFS. If this is the first time running the panel this is normal as not config has been saved yet.");
    ESP_LOGI("Main", "Default config values has been applied, will save those to create a config file.");
    esp_err_t config_save_result = ConfigManager::save_config();
    if (config_save_result != ESP_OK) {
      ESP_LOGE("Main", "Failed to save config to LittleFS, got error %s!", esp_err_to_name(config_save_result));
    }
  } else {
    if (ConfigManager::wifi_ssid.empty()) {
      ESP_LOGE("Main", "Successfully loaded config from LittleFS but the config is not valid. Empty WiFi SSID, will load default values and start Access Point.");
      ConfigManager::create_default();

      WiFiManager::start_ap(&ConfigManager::wifi_hostname);
    } else {
      ESP_LOGI("Main", "Config loaded successfully. Starting NSPanel as '%s'.", ConfigManager::wifi_hostname.c_str());
      // Set global log level
      esp_log_level_set("*", static_cast<esp_log_level_t>(ConfigManager::log_level));

      // Start task that handles WiFi connection
      WiFiManager::start_client(&ConfigManager::wifi_ssid, &ConfigManager::wifi_psk, &ConfigManager::wifi_hostname);
    }
  }

  // Only start managers for actual functionality if MQTT is configured.
  if (!ConfigManager::mqtt_server.empty()) {
    // Start task that handles MQTT connection
    MqttManager::start(&ConfigManager::mqtt_server, &ConfigManager::mqtt_port, &ConfigManager::mqtt_username, &ConfigManager::mqtt_password);

    // MQTT is now setup, enable custom logging through MQTT
    publish_mqtt_log_messages_queue = xQueueCreate(16, sizeof(char *));
    xTaskCreatePinnedToCore(task_publish_mqtt_log_message, "pub_mqtt_log", 4096, NULL, 3, &task_publish_mqtt_log_message_handle, 1);
    mqtt_log_topic = std::format("nspanel/{}/log", WiFiManager::mac_string());
    esp_log_set_vprintf(custom_log_vprintf);

    // Start RoomManager
    RoomManager::init();

    // Start manager to handles temperature and status updates
    StatusUpdateManager::init();
  }

  // Start task that handles the HTTP server
  WebManager::start();

  // Start the interface and load config
  InterfaceManager::init();

  // Wait until we have been accepted by a manager and received both address and port
  while (NSPM_ConfigManager::get_manager_address().empty() && NSPM_ConfigManager::get_manager_port() == 0) {
    vTaskDelay(pdMS_TO_TICKS(250));
  }

  // Hook into update manager
  UpdateManager::init();
}