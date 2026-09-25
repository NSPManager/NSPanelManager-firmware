// Link-time fakes: these replace the real translation units for collaborators the
// unit under test only talks to through their public static API.
#include <ConfigManager.hpp>
#include <MqttManager.hpp>
#include <NSPM_ConfigManager.hpp>
#include <WiFiManager.hpp>

#include <string>
#include <vector>

// ---- test-visible spy state -------------------------------------------------
namespace fake {
std::vector<std::string> mqtt_subscribed;
std::vector<std::string> mqtt_unsubscribed;
std::vector<std::pair<std::string, std::string>> mqtt_published;
std::shared_ptr<NSPanelConfig> config;
std::string manager_address = "10.0.0.5";
uint16_t manager_port = 8000;
} // namespace fake

// ---- MqttManager ------------------------------------------------------------
void MqttManager::start(std::string *, uint16_t *, std::string *, std::string *) {}
bool MqttManager::connected() { return true; }
esp_err_t MqttManager::subscribe(std::string topic) { fake::mqtt_subscribed.push_back(topic); return ESP_OK; }
esp_err_t MqttManager::unsubscribe(std::string topic) { fake::mqtt_unsubscribed.push_back(topic); return ESP_OK; }
esp_err_t MqttManager::publish(std::string topic, const char *data, size_t len, bool) {
  fake::mqtt_published.emplace_back(topic, std::string(data, len));
  return ESP_OK;
}
esp_err_t MqttManager::register_handler(esp_mqtt_event_id_t, esp_event_handler_t, void *) { return ESP_OK; }
esp_err_t MqttManager::unregister_handler(esp_mqtt_event_id_t, esp_event_handler_t) { return ESP_OK; }

// ---- NSPM_ConfigManager -----------------------------------------------------
esp_err_t NSPM_ConfigManager::get_config(std::shared_ptr<NSPanelConfig> *out) {
  if (fake::config == nullptr) return ESP_ERR_NOT_FINISHED;
  *out = fake::config;
  return ESP_OK;
}
std::string NSPM_ConfigManager::get_manager_address() { return fake::manager_address; }
uint16_t NSPM_ConfigManager::get_manager_port() { return fake::manager_port; }

// Event bases normally defined in the collaborator .cpp files we replaced.
#include <NSPM_ConfigManager_event.hpp>
ESP_EVENT_DEFINE_BASE(NSPM_CONFIGMANAGER_EVENT);
