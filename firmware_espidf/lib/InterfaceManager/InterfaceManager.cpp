#include <ConfigManager.hpp>
#include <GUI_data.hpp>
#include <HomePage.hpp>
#include <InterfaceManager.hpp>
#include <LoadingPage.hpp>
#include <MqttManager.hpp>
#include <NSPM_ConfigManager.hpp>
#include <Nextion.hpp>
#include <Nextion_event.hpp>
#include <RoomManager.hpp>
#include <RoomManager_event.hpp>
#include <ScreensaverPage.hpp>
#include <WiFiManager.hpp>
#include <esp_log.h>

void InterfaceManager::init() {
  esp_log_level_set("InterfaceManager", esp_log_level_t::ESP_LOG_DEBUG); // TODO: Load from config
  current_page_unshow_callback.set(NULL);
  InterfaceManager::_unshow_queue = xQueueCreate(4, sizeof(std::function<void()>));
  esp_err_t nextion_init_result = Nextion::init();
  if (nextion_init_result != ESP_OK) {
    ESP_LOGE("InterfaceManager", "Failed to initialize Nextion display. Will not continue with InterfaceManager!");
    return;
  }

  esp_event_handler_register(NEXTION_EVENT, ESP_EVENT_ANY_ID, InterfaceManager::_nextion_event_handler, NULL);
  RoomManager::register_handler(ESP_EVENT_ANY_ID, InterfaceManager::_room_manager_event_handler, NULL);

  // Show boot page
  LoadingPage::show();

  // No SSID configured, the access point has been started through WiFiManager
  // Show text to connect to AP and wait indefinefly. Panel will reboot once settings has been saved.
  // TODO: Perhaps implement flag in WiFi manager for which more the we are currently in
  // instead of relying on empty WiFi SSID.
  if (ConfigManager::wifi_ssid.empty()) {
    for (;;) {
      std::string primary_text = "Connect to ";
      primary_text.append(ConfigManager::wifi_hostname);
      LoadingPage::set_loading_text(primary_text);

      char ip_address_str[IP4ADDR_STRLEN_MAX];
      esp_netif_ip_info_t ip_info = WiFiManager::ip_info();
      sprintf(ip_address_str, IPSTR, IP2STR(&ip_info.ip));
      LoadingPage::set_secondary_text(ip_address_str);

      vTaskDelay(pdMS_TO_TICKS(1000)); // Update every second
    }
  }

  // Wait for WiFi
  std::string append_string = "";
  if (!WiFiManager::connected() || WiFiManager::ip_info().ip.addr == 0) {
    std::string connection_text = "Connecting to ";
    connection_text.append(ConfigManager::wifi_ssid);

    std::string set_string = connection_text;
    set_string.append(append_string);
    LoadingPage::set_loading_text(set_string);
    LoadingPage::set_secondary_text("");

    if (append_string.size() < 3) {
      append_string.append(".");
    } else {
      append_string.clear();
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }

  // Wait for MQTT
  append_string.clear();
  while (!MqttManager::connected()) {
    std::string connection_text = "Connecting to MQTT";
    char ip_address_str[IP4ADDR_STRLEN_MAX];
    esp_netif_ip_info_t ip_info = WiFiManager::ip_info();
    sprintf(ip_address_str, IPSTR, IP2STR(&ip_info.ip));

    std::string set_string = connection_text;
    set_string.append(append_string);
    LoadingPage::set_loading_text(set_string);
    LoadingPage::set_secondary_text(ip_address_str);

    if (append_string.size() < 3) {
      append_string.append(".");
    } else {
      append_string.clear();
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }

  LoadingPage::set_loading_text("Loading config");
  NSPM_ConfigManager::init(); // Register to manager and load all config

  ESP_LOGI("InterfaceManager", "Interface manager init complete. Waiting for rooms to load.");

  // WiFi and MQTT connected.
  // RoomManager will take over and load the config, once the config has been
  // successfully loaded the event handler for RoomManager will take over and send the Nextion display to the correct page
}

void InterfaceManager::call_unshow_callback() {
  auto unshow_handle = InterfaceManager::current_page_unshow_callback.get();
  if (unshow_handle != nullptr) {
    xQueueSend(InterfaceManager::_unshow_queue, &unshow_handle, pdMS_TO_TICKS(500));
    xTaskCreatePinnedToCore(InterfaceManager::_task_unshow_page, "unshow_task", 4096, NULL, 6, NULL, 1);
  }
}

void InterfaceManager::_task_unshow_page(void *param) {
  std::function<void()> unshow_handle;
  while (xQueueReceive(InterfaceManager::_unshow_queue, &unshow_handle, pdMS_TO_TICKS(250)) == pdPASS) {
    unshow_handle();
  }
  vTaskDelete(NULL);
}

void InterfaceManager::_nextion_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  switch (event_id) {
  case nextion_event_t::SLEEP_EVENT: {
    ScreensaverPage::show();
    break;
  }

  case nextion_event_t::WAKE_EVENT: {
    // Someone touched the screensaver, unshow it and go to the default page, whatever is selected in the manager
    NSPanelConfig config;
    if (NSPM_ConfigManager::get_config(&config) == ESP_OK) {
      Nextion::set_brightness_level(config.screen_dim_level, 1000);
      if (config.default_page == 0) { // TODO: Convert to protobuf ENUM for clarity
        HomePage::show();             // TODO: Show the user selected first page
      } else {
        ESP_LOGE("ScreensaverPage", "Unknown default page %ld, will default to home page!", config.default_page);
        HomePage::show();
      }
    } else {
      ESP_LOGE("ScreensaverPage", "Failed to get NSPanel Config when unshowing screensaver page!");
    }
    break;
  }

  default:
    break;
  }
}

void InterfaceManager::_room_manager_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  // Received event that all rooms has been loaded AND we are currently on the loading page.
  // This should only happen on first boot of panel, force navigate to Home page.
  if (event_id == roommanager_event_t::ALL_ROOMS_LOADED && LoadingPage::showing()) {
    ESP_LOGI("InterfaceManager", "Loaded all rooms and currently showing loading page. Will show HomePage.");
    HomePage::show(); // TODO: Show the user selected first page.
  }
}