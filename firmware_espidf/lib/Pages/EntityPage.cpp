#include <ConfigManager.hpp>
#include <EntityPage.hpp>
#include <GUI_data.hpp>
#include <InterfaceManager.hpp>
#include <MqttManager.hpp>
#include <Nextion.hpp>
#include <Nextion_event.hpp>
#include <esp_log.h>
#include <protobuf_nspanel_entity.pb-c.h>

void EntityPage::show(std::string state_topic) {
  esp_log_level_set("EntityPage", ConfigManager::log_level);

  if (EntityPage::_current_state_mutex == NULL) {
    EntityPage::_current_state_mutex = xSemaphoreCreateMutex();
  }

  InterfaceManager::call_unshow_callback();
  InterfaceManager::current_page_unshow_callback.set(EntityPage::unshow);

  esp_event_handler_register(NEXTION_EVENT, ESP_EVENT_ANY_ID, &EntityPage::_handle_nextion_event, NULL);
  MqttManager::register_handler(MQTT_EVENT_ANY, &EntityPage::_handle_mqtt_event, NULL);

  EntityPage::_current_entity_mqtt_topic = state_topic;
  MqttManager::subscribe(EntityPage::_current_entity_mqtt_topic);
}

void EntityPage::unshow() {
  MqttManager::unregister_handler(MQTT_EVENT_ANY, &EntityPage::_handle_mqtt_event);
  if (!EntityPage::_current_entity_mqtt_topic.empty()) {
    MqttManager::unsubscribe(EntityPage::_current_entity_mqtt_topic);
  }
  EntityPage::_currently_showing = false;
}

void EntityPage::_handle_mqtt_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id == MQTT_EVENT_DATA) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    if (event->topic_len == 0 || event->data_len == 0) [[unlikely]] {
      return;
    }
    std::string topic_string = std::string(event->topic, event->topic_len);
    if (topic_string.compare(EntityPage::_current_entity_mqtt_topic) == 0) {
      NSPanelEntityState *new_state = nspanel_entity_state__unpack(NULL, event->data_len, (const uint8_t *)event->data);
      if (new_state != NULL) [[likely]] {
        if (xSemaphoreTake(EntityPage::_current_state_mutex, pdMS_TO_TICKS(500)) == pdPASS) [[likely]] {
          EntityPage::_current_state = std::shared_ptr<NSPanelEntityState>(new_state, &EntityPage::_delete_nspanel_entity_state_object);
          EntityPage::_update_display();
        } else {
          ESP_LOGE("EntityPage", "Failed to take mutex to update current state.");
        }
      } else {
        ESP_LOGE("EntityPage", "Received new state but failed to parse into protobuf object.");
      }
    }
  } else if (event_id == MQTT_EVENT_CONNECTED) {
    MqttManager::subscribe(EntityPage::_current_entity_mqtt_topic);
  }
}

void EntityPage::_update_display() {
  switch (EntityPage::_current_state->entity_case) {
  case NSPANEL_ENTITY_STATE__ENTITY_LIGHT:
    EntityPage::_update_display_light();
    break;

  default:
    ESP_LOGE("EntityPage", "Unknown state type. Can't call appropriate update display function.");
    break;
  }
}

void EntityPage::_update_display_light() {
  if (!EntityPage::_currently_showing) {
    EntityPage::_currently_showing = true;
    // Nextion::go_to_page(GUI_)
  }
}

void EntityPage::_delete_nspanel_entity_state_object(NSPanelEntityState *object) {
  nspanel_entity_state__free_unpacked(object, NULL);
}