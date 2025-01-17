#include "RoomManager.hpp"
#include <MqttManager.hpp>
#include <NSPM_ConfigManager.hpp>
#include <NSPM_ConfigManager_event.hpp>
#include <RoomManager_event.hpp>
#include <UpdateManager_event.hpp>
#include <WiFiManager.hpp>
#include <esp_event.h>
#include <esp_log.h>
#include <format>
#include <vector>

ESP_EVENT_DEFINE_BASE(ROOMMANAGER_EVENT);

void RoomManager::init() {
  ESP_LOGI("RoomManager", "Initializing RoomManager.");
  esp_log_level_set("RoomManager", esp_log_level_t::ESP_LOG_DEBUG); // TODO: Read from config
  RoomManager::_home_page_mutex = xSemaphoreCreateMutex();
  RoomManager::_entities_page_mutex = xSemaphoreCreateMutex();
  RoomManager::_load_all_rooms_task_handle = NULL;

  // Initialize local event loop
  RoomManager::_local_event_loop_args = {
      .queue_size = 128,
      .task_name = "roommanager_event_loop",
      .task_priority = 6,
      .task_stack_size = 8192,
      .task_core_id = 1};
  esp_event_loop_create(&RoomManager::_local_event_loop_args, &RoomManager::_local_event_loop);

  // Hook into MQTT events
  MqttManager::register_handler(MQTT_EVENT_DATA, RoomManager::_mqtt_event_handler, NULL);
  MqttManager::register_handler(MQTT_EVENT_CONNECTED, RoomManager::_mqtt_event_handler_connected, NULL);
}

esp_err_t RoomManager::get_home_page_status(std::shared_ptr<NSPanelRoomStatus> *status) {
  if (xSemaphoreTake(RoomManager::_home_page_mutex, pdMS_TO_TICKS(250) == pdPASS)) {
    *status = RoomManager::_home_page;
    xSemaphoreGive(RoomManager::_home_page_mutex);
    return ESP_OK;
  } else {
    ESP_LOGE("RoomManager", "Failed to get _home_page_mutex while trying to get NSPanelRoomStatus!");
  }
  return ESP_ERR_NOT_FINISHED;
}

esp_err_t RoomManager::get_home_page_status_all_rooms(std::shared_ptr<NSPanelRoomStatus> *status) {
  if (xSemaphoreTake(RoomManager::_home_page_mutex, pdMS_TO_TICKS(250) == pdPASS)) {
    *status = RoomManager::_home_page_all_rooms;
    xSemaphoreGive(RoomManager::_home_page_mutex);
    return ESP_OK;
  } else {
    ESP_LOGE("RoomManager", "Failed to get _home_page_mutex while trying to get NSPanelRoomStatus for all rooms!");
  }
  return ESP_ERR_NOT_FINISHED;
}

esp_err_t RoomManager::get_home_page_status_mutable(std::shared_ptr<NSPanelRoomStatus> *status) {
  if (xSemaphoreTake(RoomManager::_home_page_mutex, pdMS_TO_TICKS(250) == pdPASS)) {
    size_t status_pack_size = nspanel_room_status__get_packed_size(RoomManager::_home_page.get());
    std::vector<uint8_t> buffer(status_pack_size);
    nspanel_room_status__pack(RoomManager::_home_page.get(), buffer.data());
    NSPanelRoomStatus *temp_status;

    temp_status = nspanel_room_status__unpack(NULL, status_pack_size, buffer.data());
    if (temp_status != NULL) [[likely]] {
      xSemaphoreGive(RoomManager::_home_page_mutex);
      (*status) = std::shared_ptr<NSPanelRoomStatus>(temp_status, &RoomManager::_nspanel_room_status_shared_ptr_deleter);
      return ESP_OK;
    } else {
      xSemaphoreGive(RoomManager::_home_page_mutex);
      return ESP_ERR_NOT_FINISHED;
    }
  } else {
    ESP_LOGE("RoomManager", "Failed to get _home_page_mutex while trying to get NSPanelRoomStatus!");
  }
  return ESP_ERR_NOT_FINISHED;
}

esp_err_t RoomManager::get_home_page_status_mutable_all_rooms(std::shared_ptr<NSPanelRoomStatus> *status) {
  if (xSemaphoreTake(RoomManager::_home_page_mutex, pdMS_TO_TICKS(250) == pdPASS)) {
    size_t status_pack_size = nspanel_room_status__get_packed_size(RoomManager::_home_page_all_rooms.get());
    std::vector<uint8_t> buffer(status_pack_size);
    nspanel_room_status__pack(RoomManager::_home_page_all_rooms.get(), buffer.data());
    NSPanelRoomStatus *temp_status;

    temp_status = nspanel_room_status__unpack(NULL, status_pack_size, buffer.data());
    if (temp_status != NULL) [[likely]] {
      xSemaphoreGive(RoomManager::_home_page_mutex);
      (*status) = std::shared_ptr<NSPanelRoomStatus>(temp_status, &RoomManager::_nspanel_room_status_shared_ptr_deleter);
      return ESP_OK;
    } else {
      xSemaphoreGive(RoomManager::_home_page_mutex);
      return ESP_ERR_NOT_FINISHED;
    }
  } else {
    ESP_LOGE("RoomManager", "Failed to get _home_page_mutex while trying to get NSPanelRoomStatus for all rooms!");
  }
  return ESP_ERR_NOT_FINISHED;
}

esp_err_t RoomManager::go_to_previous_room() {
  std::shared_ptr<NSPanelConfig> config;
  if (NSPM_ConfigManager::get_config(&config) == ESP_OK) {
    NSPanelMQTTManagerCommand command = NSPANEL_MQTTMANAGER_COMMAND__INIT;
    NSPanelMQTTManagerCommand__PreviousRoom previous_room_cmd = NSPANEL_MQTTMANAGER_COMMAND__PREVIOUS_ROOM__INIT;
    previous_room_cmd.nspanel_id = config->nspanel_id;
    command.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_PREVIOUS_ROOM;
    command.previous_room = &previous_room_cmd;

    // Serialize
    size_t packed_cmd_size = nspanel_mqttmanager_command__get_packed_size(&command);
    std::vector<uint8_t> buffer(packed_cmd_size);
    if (nspanel_mqttmanager_command__pack(&command, buffer.data()) == packed_cmd_size) {
      esp_err_t pub_res = MqttManager::publish(NSPM_ConfigManager::get_manager_command_topic(), (char *)buffer.data(), buffer.size(), false);
      if (pub_res != ESP_OK) {
        ESP_LOGE("RoomManager", "Failed to publish command to go to previous room. Error: %s", esp_err_to_name(pub_res));
        return ESP_ERR_NOT_FINISHED;
      }
      return ESP_OK;
    } else {
      ESP_LOGE("RoomManager", "Failed to pack command to go to previous room!");
      return ESP_ERR_NOT_FINISHED;
    }
  } else {
    ESP_LOGE("RoomManager", "Failed to get config while trying to send command to manager.");
  }
  return ESP_ERR_NOT_FINISHED;
}

esp_err_t RoomManager::go_to_next_room() {
  std::shared_ptr<NSPanelConfig> config;
  if (NSPM_ConfigManager::get_config(&config) == ESP_OK) {
    NSPanelMQTTManagerCommand command = NSPANEL_MQTTMANAGER_COMMAND__INIT;
    NSPanelMQTTManagerCommand__NextRoom next_room_cmd = NSPANEL_MQTTMANAGER_COMMAND__NEXT_ROOM__INIT;
    next_room_cmd.nspanel_id = config->nspanel_id;
    command.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_NEXT_ROOM;
    command.next_room = &next_room_cmd;

    // Serialize
    size_t packed_cmd_size = nspanel_mqttmanager_command__get_packed_size(&command);
    std::vector<uint8_t> buffer(packed_cmd_size);
    if (nspanel_mqttmanager_command__pack(&command, buffer.data()) == packed_cmd_size) {
      esp_err_t pub_res = MqttManager::publish(NSPM_ConfigManager::get_manager_command_topic(), (char *)buffer.data(), buffer.size(), false);
      if (pub_res != ESP_OK) {
        ESP_LOGE("RoomManager", "Failed to publish command to go to next room. Error: %s", esp_err_to_name(pub_res));
        return ESP_ERR_NOT_FINISHED;
      }
      return ESP_OK;
    } else {
      ESP_LOGE("RoomManager", "Failed to pack command to go to next room!");
      return ESP_ERR_NOT_FINISHED;
    }
  } else {
    ESP_LOGE("RoomManager", "Failed to get config while trying to send command to manager.");
  }
  return ESP_ERR_NOT_FINISHED;
}

esp_err_t RoomManager::replace_home_page_status(std::shared_ptr<NSPanelRoomStatus> status) {
  if (xSemaphoreTake(RoomManager::_home_page_mutex, pdMS_TO_TICKS(250) == pdPASS)) {
    RoomManager::_home_page = status;
    xSemaphoreGive(RoomManager::_home_page_mutex);

    esp_event_post(ROOMMANAGER_EVENT, roommanager_event_t::HOME_PAGE_UPDATED, NULL, 0, pdMS_TO_TICKS(250));
    return ESP_OK;
  }
  return ESP_ERR_NOT_FINISHED;
}

esp_err_t RoomManager::replace_home_page_status_all_rooms(std::shared_ptr<NSPanelRoomStatus> status) {
  if (xSemaphoreTake(RoomManager::_home_page_mutex, pdMS_TO_TICKS(250) == pdPASS)) {
    RoomManager::_home_page_all_rooms = status;
    xSemaphoreGive(RoomManager::_home_page_mutex);

    esp_event_post(ROOMMANAGER_EVENT, roommanager_event_t::HOME_PAGE_UPDATED, NULL, 0, pdMS_TO_TICKS(250));
    return ESP_OK;
  }
  return ESP_ERR_NOT_FINISHED;
}

esp_err_t RoomManager::get_current_room_entities_page_status(std::shared_ptr<NSPanelRoomEntitiesPage> *status) {
  if (xSemaphoreTake(RoomManager::_entities_page_mutex, pdMS_TO_TICKS(250) == pdPASS)) {
    *status = RoomManager::_entities_page;
    xSemaphoreGive(RoomManager::_entities_page_mutex);
    return ESP_OK;
  }
  return ESP_ERR_NOT_FINISHED;
}

esp_err_t RoomManager::go_to_next_entities_page() {
  std::shared_ptr<NSPanelConfig> config;
  if (NSPM_ConfigManager::get_config(&config) == ESP_OK) {
    NSPanelMQTTManagerCommand command = NSPANEL_MQTTMANAGER_COMMAND__INIT;
    NSPanelMQTTManagerCommand__NextEntitiesPage next_entities_page = NSPANEL_MQTTMANAGER_COMMAND__NEXT_ENTITIES_PAGE__INIT;
    next_entities_page.nspanel_id = config->nspanel_id;
    command.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_NEXT_ENTITIES_PAGE;
    command.next_entities_page = &next_entities_page;

    // Serialize
    size_t packed_cmd_size = nspanel_mqttmanager_command__get_packed_size(&command);
    std::vector<uint8_t> buffer(packed_cmd_size);
    if (nspanel_mqttmanager_command__pack(&command, buffer.data()) == packed_cmd_size) {
      esp_err_t pub_res = MqttManager::publish(NSPM_ConfigManager::get_manager_command_topic(), (char *)buffer.data(), buffer.size(), false);
      if (pub_res != ESP_OK) {
        ESP_LOGE("RoomManager", "Failed to publish command to go to next page. Error: %s", esp_err_to_name(pub_res));
        return ESP_ERR_NOT_FINISHED;
      }
      return ESP_OK;
    } else {
      ESP_LOGE("RoomManager", "Failed to pack command to go to next page!");
      return ESP_ERR_NOT_FINISHED;
    }
  } else {
    ESP_LOGE("RoomManager", "Failed to get config while trying to send command to manager.");
  }

  return ESP_ERR_NOT_FINISHED;
}

esp_err_t RoomManager::go_to_previous_entities_page() {
  std::shared_ptr<NSPanelConfig> config;
  if (NSPM_ConfigManager::get_config(&config) == ESP_OK) {
    NSPanelMQTTManagerCommand command = NSPANEL_MQTTMANAGER_COMMAND__INIT;
    NSPanelMQTTManagerCommand__PreviousEntitiesPage previous_entities_page = NSPANEL_MQTTMANAGER_COMMAND__PREVIOUS_ENTITIES_PAGE__INIT;
    previous_entities_page.nspanel_id = config->nspanel_id;
    command.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_PREVIOUS_ENTITIES_PAGE;
    command.previous_entities_page = &previous_entities_page;

    // Serialize
    size_t packed_cmd_size = nspanel_mqttmanager_command__get_packed_size(&command);
    std::vector<uint8_t> buffer(packed_cmd_size);
    if (nspanel_mqttmanager_command__pack(&command, buffer.data()) == packed_cmd_size) {
      esp_err_t pub_res = MqttManager::publish(NSPM_ConfigManager::get_manager_command_topic(), (char *)buffer.data(), buffer.size(), false);
      if (pub_res != ESP_OK) {
        ESP_LOGE("RoomManager", "Failed to publish command to go to next page. Error: %s", esp_err_to_name(pub_res));
        return ESP_ERR_NOT_FINISHED;
      }
      return ESP_OK;
    } else {
      ESP_LOGE("RoomManager", "Failed to pack command to go to next page!");
      return ESP_ERR_NOT_FINISHED;
    }
  } else {
    ESP_LOGE("RoomManager", "Failed to get config while trying to send command to manager.");
  }
  return ESP_ERR_NOT_FINISHED;
}

esp_err_t RoomManager::register_handler(int32_t event_id, esp_event_handler_t event_handler, void *event_handler_arg) {
  if (RoomManager::_local_event_loop != NULL) {
    return esp_event_handler_register_with(RoomManager::_local_event_loop, ROOMMANAGER_EVENT, event_id, event_handler, event_handler_arg);
  } else {
    return ESP_ERR_NOT_FINISHED;
  }
}

esp_err_t RoomManager::unregister_handler(int32_t event_id, esp_event_handler_t event_handler) {
  if (RoomManager::_local_event_loop != NULL) {
    return esp_event_handler_unregister_with(RoomManager::_local_event_loop, ROOMMANAGER_EVENT, event_id, event_handler);
  } else {
    return ESP_ERR_NOT_FINISHED;
  }
}

void RoomManager::_mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (!RoomManager::_load_new_rooms) {
    return;
  }

  // This function is only registered for MQTT_EVENT_DATA, handle data config data:
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
  std::string topic_string = std::string(event->topic, event->topic_len);

  std::string base_topic = "nspanel/";
  base_topic.append(WiFiManager::mac_string());

  std::string home_page_topic = base_topic;
  home_page_topic.append("/home_page");
  std::string home_page_all_rooms_topic = base_topic;
  home_page_all_rooms_topic.append("/home_page_all");
  std::string entities_page_topic = base_topic;
  entities_page_topic.append("/entities_page");

  if (topic_string.compare(home_page_topic) == 0) {
    NSPanelRoomStatus *room_status = nspanel_room_status__unpack(NULL, event->data_len, (const uint8_t *)event->data);
    if (room_status != NULL) {
      if (xSemaphoreTake(RoomManager::_home_page_mutex, pdMS_TO_TICKS(250)) == pdPASS) {
        ESP_LOGD("RoomManager", "Received new home page state update.");
        RoomManager::_home_page = std::shared_ptr<NSPanelRoomStatus>(room_status, &RoomManager::_nspanel_room_status_shared_ptr_deleter);
        xSemaphoreGive(RoomManager::_home_page_mutex);

        if (esp_event_post_to(RoomManager::_local_event_loop, ROOMMANAGER_EVENT, roommanager_event_t::HOME_PAGE_UPDATED, NULL, 0, pdMS_TO_TICKS(250)) != ESP_OK) {
          ESP_LOGW("RoomManager", "Failed to publish event that new home page data is available.");
        }
      } else {
        ESP_LOGE("RoomManager", "Got new status for home page but couldn't take mutex to update it! Will free new state.");
        nspanel_room_status__free_unpacked(room_status, NULL);
      }
    } else {
      ESP_LOGE("RoomManager", "Got new status for home page but failed to unpack it.");
    }
  } else if (topic_string.compare(home_page_all_rooms_topic) == 0) {
    NSPanelRoomStatus *room_status = nspanel_room_status__unpack(NULL, event->data_len, (const uint8_t *)event->data);
    if (room_status != NULL) {
      if (xSemaphoreTake(RoomManager::_home_page_mutex, pdMS_TO_TICKS(250)) == pdPASS) {
        ESP_LOGD("RoomManager", "Received new home page state for all rooms.");
        RoomManager::_home_page_all_rooms = std::shared_ptr<NSPanelRoomStatus>(room_status, &RoomManager::_nspanel_room_status_shared_ptr_deleter);
        xSemaphoreGive(RoomManager::_home_page_mutex);

        if (esp_event_post_to(RoomManager::_local_event_loop, ROOMMANAGER_EVENT, roommanager_event_t::HOME_PAGE_UPDATED, NULL, 0, pdMS_TO_TICKS(250)) != ESP_OK) {
          ESP_LOGW("RoomManager", "Failed to publish event that new home page data is available for all rooms.");
        }
      } else {
        ESP_LOGE("RoomManager", "Got new status for home page (all rooms) but couldn't take mutex to update it! Will free new state.");
        nspanel_room_status__free_unpacked(room_status, NULL);
      }
    } else {
      ESP_LOGE("RoomManager", "Got new status for home page but failed to unpack it.");
    }
  } else if (topic_string.compare(entities_page_topic) == 0) {
    NSPanelRoomEntitiesPage *entities_page = nspanel_room_entities_page__unpack(NULL, event->data_len, (const uint8_t *)event->data);
    if (entities_page != NULL) {
      if (xSemaphoreTake(RoomManager::_entities_page_mutex, pdMS_TO_TICKS(250)) == pdPASS) {
        ESP_LOGD("RoomManager", "Received new entities page state update.");
        RoomManager::_entities_page = std::shared_ptr<NSPanelRoomEntitiesPage>(entities_page, &RoomManager::_nspanel_room_entities_page_shared_ptr_deleter);
        xSemaphoreGive(RoomManager::_entities_page_mutex);

        if (esp_event_post_to(RoomManager::_local_event_loop, ROOMMANAGER_EVENT, roommanager_event_t::ROOM_ENTITIES_PAGE_UPDATED, NULL, 0, pdMS_TO_TICKS(250)) != ESP_OK) {
          ESP_LOGW("RoomManager", "Failed to publish event that new entities page is available.");
        }
      } else {
        ESP_LOGE("RoomManager", "Got new status for entities page but couldn't take mutex to update it! Will free unpacked data.");
        nspanel_room_entities_page__free_unpacked(entities_page, NULL);
      }
    } else {
      ESP_LOGE("RoomManager", "Got new status for entities page but failed to unpack it.");
    }
  }
}

void RoomManager::_mqtt_event_handler_connected(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  ESP_LOGI("RoomManager", "MQTT connected. Subscribing to room status topics.");
  // Subscribe to relevant MQTT topics.
  std::string mqtt_base_topic = "nspanel/";
  mqtt_base_topic.append(WiFiManager::mac_string());

  std::string home_page_topic = mqtt_base_topic;
  home_page_topic.append("/home_page");
  std::string home_page_all_rooms_topic = mqtt_base_topic;
  home_page_all_rooms_topic.append("/home_page_all");
  std::string entities_page_topic = mqtt_base_topic;
  entities_page_topic.append("/entities_page");

  MqttManager::subscribe(home_page_topic);
  MqttManager::subscribe(home_page_all_rooms_topic);
  MqttManager::subscribe(entities_page_topic);
}

void RoomManager::_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
}

void RoomManager::_nspanel_room_status_shared_ptr_deleter(NSPanelRoomStatus *status) {
  nspanel_room_status__free_unpacked(status, NULL);
}

void RoomManager::_nspanel_room_entities_page_shared_ptr_deleter(NSPanelRoomEntitiesPage *status) {
  nspanel_room_entities_page__free_unpacked(status, NULL);
}