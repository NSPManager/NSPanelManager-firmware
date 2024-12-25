#include <MqttManager.hpp>
#include <NSPM_ConfigManager.hpp>
#include <WiFiManager.hpp>
#include <cJSON.h>
#include <esp_log.h>

void MqttManager::start(std::string *server, uint16_t *port, std::string *username, std::string *password) {
  esp_log_level_set("MqttManager", esp_log_level_t::ESP_LOG_DEBUG); // TODO: Load from config
  ESP_LOGI("MqttManager", "Starting MQTTManager, will connect to %s:%d", server->c_str(), *port);
  MqttManager::_connected = false;
  MqttManager::_mqtt_config.broker.address.hostname = server->c_str();
  MqttManager::_mqtt_config.broker.address.port = *port;
  MqttManager::_mqtt_config.broker.address.transport = esp_mqtt_transport_t::MQTT_TRANSPORT_OVER_TCP;
  if (username->size() > 0 && password->size() > 0) {
    MqttManager::_mqtt_config.credentials.username = username->c_str();
    MqttManager::_mqtt_config.credentials.authentication.password = password->c_str();
  }
  std::string mqtt_client_id = "NSPMPanel-";
  mqtt_client_id.append(WiFiManager::mac_string());

  MqttManager::_mqtt_config.credentials.client_id = mqtt_client_id.c_str();
  MqttManager::_mqtt_config.buffer.size = 4096;
  MqttManager::_mqtt_config.buffer.out_size = 4096;

  MqttManager::_mqtt_config.task.priority = 10;
  MqttManager::_mqtt_config.task.stack_size = 8192;

  MqttManager::_state_topic = "nspanel/";
  MqttManager::_state_topic.append(WiFiManager::mac_string());
  MqttManager::_state_topic.append("/state");

  // Create JSON object for state offline message used in last will for MQTT connection.
  cJSON *json = cJSON_CreateObject();
  if (json != NULL) {
    std::string mac_string = WiFiManager::mac_string();
    cJSON_AddStringToObject(json, "mac", mac_string.c_str());
    cJSON_AddStringToObject(json, "state", "offline");
  } else {
    ESP_LOGE("MqttManager", "Failed to create cJSON object when trying to send online state update!");
    return;
  }

  // Format JSON to string
  char *json_string = cJSON_Print(json);
  MqttManager::_last_will_message = json_string;
  cJSON_free(json);

  // Set last will message in config and update config of client
  MqttManager::_mqtt_config.session.last_will.msg = MqttManager::_last_will_message.c_str();
  MqttManager::_mqtt_config.session.last_will.msg_len = MqttManager::_last_will_message.length();
  MqttManager::_mqtt_config.session.last_will.topic = MqttManager::_state_topic.c_str();
  MqttManager::_mqtt_config.session.last_will.retain = true;
  MqttManager::_mqtt_config.session.last_will.qos = 0;

  // Initialize MQTT client with built config
  MqttManager::_mqtt_client = esp_mqtt_client_init(&MqttManager::_mqtt_config);
  if (MqttManager::_mqtt_client == NULL) {
    ESP_LOGE("MqttManager", "Failed to create MQTT client!");
    esp_restart();
    return;
  }

  // Register event handler for MQTT events
  esp_err_t result = esp_mqtt_client_register_event(MqttManager::_mqtt_client, esp_mqtt_event_id_t::MQTT_EVENT_ANY, MqttManager::_mqtt_event_handler, NULL);
  switch (result) {
  case ESP_ERR_NO_MEM:
    ESP_LOGE("MqttManager", "Failed to allocate MQTT event handler!");
    esp_restart();
    break;

  case ESP_ERR_INVALID_ARG:
    ESP_LOGE("MqttManager", "Failed to initialize MQTT event handler!");
    esp_restart();
    break;

  case ESP_OK:
    ESP_LOGV("MqttManager", "Attached MQTT event handler.");
    break;

  default:
    ESP_LOGW("MqttManager", "Unknown status code when registering MQTT event handler: %s", esp_err_to_name(result));
    break;
  }

  // Start the MQTT client
  result = esp_mqtt_client_start(MqttManager::_mqtt_client);
  switch (result) {
  case ESP_ERR_INVALID_ARG:
    ESP_LOGE("MqttManager", "Failed to start MQTT client!");
    esp_restart();
    break;

  case ESP_OK:
    ESP_LOGI("MqttManager", "Started MQTT client.");
    break;

  default:
    ESP_LOGW("MqttManager", "Unknown status code when registering MQTT event handler: %s", esp_err_to_name(result));
    break;
  }
}

void MqttManager::_mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
  // esp_mqtt_client_handle_t client = event->client;

  switch ((esp_mqtt_event_id_t)event_id) {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI("MqttManager", "Connected to MQTT server.");
    MqttManager::_connected = true;
    MqttManager::_send_mqtt_online_update();
    break;

  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGW("MqttManager", "Lost connection to MQTT server.");
    MqttManager::_connected = false;
    break;

  case MQTT_EVENT_ERROR:
    ESP_LOGI("MqttManager", "MQTT_EVENT_ERROR");
    if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
      if (event->error_handle->esp_tls_last_esp_err != 0) {
        ESP_LOGE("MqttManager", "Error report from esp-tls!");
      } else if (event->error_handle->esp_tls_stack_err != 0) {
        ESP_LOGE("MqttManager", "Error report from tls stack!");
      } else if (event->error_handle->esp_transport_sock_errno != 0) {
        ESP_LOGE("MqttManager", "Captured as transport's socket errno!");
      }
      ESP_LOGI("MqttManager", "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
    }
    break;

  default:
    break;
  }
}

void MqttManager::_send_mqtt_online_update() {
  if (!MqttManager::_state_topic.empty()) {
    cJSON *json = cJSON_CreateObject();
    if (json != NULL) {
      cJSON_AddStringToObject(json, "mac", WiFiManager::mac_string());
      cJSON_AddStringToObject(json, "state", "online");
    } else {
      ESP_LOGE("MqttManager", "Failed to create cJSON object when trying to send online state update!");
      return;
    }

    char *json_string = cJSON_Print(json);
    if (MqttManager::publish(MqttManager::_state_topic, json_string, strlen(json_string), false) != ESP_OK) {
      ESP_LOGE("MqttManager", "Failed to send online state update to topic %s!", MqttManager::_state_topic.c_str());
    }

    cJSON_free(json);
  }
}

bool MqttManager::connected() {
  return MqttManager::_connected;
}

esp_err_t MqttManager::subscribe(std::string topic) {
  if (MqttManager::connected()) {
    int result_code = esp_mqtt_client_subscribe_single(MqttManager::_mqtt_client, topic.c_str(), 2);
    if (result_code >= 0) {
      return ESP_OK;
    } else {
      ESP_LOGE("MqttManager", "Failed to subscribe to '%s'. Got return code: %d", topic.c_str(), result_code);
    }
  } else {
    ESP_LOGE("MqttManager", "Failed to subscribe to MQTT topic. Not connected to MQTT server.");
  }
  return ESP_ERR_NOT_FINISHED;
}

esp_err_t MqttManager::unsubscribe(std::string topic) {
  if (MqttManager::connected()) {
    int result_code = esp_mqtt_client_unsubscribe(MqttManager::_mqtt_client, topic.c_str());
    if (result_code >= 0) {
      return ESP_OK;
    } else {
      ESP_LOGE("MqttManager", "Failed to unsubscribe from '%s'. Got return code: %d", topic.c_str(), result_code);
    }
  } else {
    ESP_LOGE("MqttManager", "Failed to unsubscribe from MQTT topic. Not connected to MQTT server.");
  }
  return ESP_ERR_NOT_FINISHED;
}

esp_err_t MqttManager::publish(std::string topic, const char *data, size_t length, bool retain) {
  if (MqttManager::connected()) {
    int result_code = esp_mqtt_client_publish(MqttManager::_mqtt_client, topic.c_str(), data, length, 0, retain);
    if (result_code >= 0) {
      return ESP_OK;
    } else {
      ESP_LOGE("MqttManager", "Failed to publish to '%s'. Got return code: %d", topic.c_str(), result_code);
    }
  } else {
    ESP_LOGE("MqttManager", "Failed to publish to MQTT topic. Not connected to MQTT server.");
  }
  return ESP_ERR_NOT_FINISHED;
}

esp_err_t MqttManager::register_handler(esp_mqtt_event_id_t event_id, esp_event_handler_t event_handler, void *event_handler_arg) {
  return esp_mqtt_client_register_event(MqttManager::_mqtt_client, event_id, event_handler, event_handler_arg);
}

esp_err_t MqttManager::unregister_handler(esp_mqtt_event_id_t event_id, esp_event_handler_t event_handler) {
  return esp_mqtt_client_unregister_event(MqttManager::_mqtt_client, event_id, event_handler);
}