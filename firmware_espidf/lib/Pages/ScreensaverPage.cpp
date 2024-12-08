#include <GUI_data.hpp>
#include <HomePage.hpp>
#include <InterfaceManager.hpp>
#include <MqttManager.hpp>
#include <NSPM_ConfigManager.hpp>
#include <NSPM_ConfigManager_event.hpp>
#include <Nextion.hpp>
#include <ScreensaverPage.hpp>
#include <esp_log.h>

void ScreensaverPage::show() {
  if (ScreensaverPage::_currently_shown.get()) {
    // Do not "show" page again when it's already showing.
    return;
  }

  InterfaceManager::call_unshow_callback();
  InterfaceManager::current_page_unshow_callback.set(ScreensaverPage::unshow);

  MqttManager::register_handler(MQTT_EVENT_DATA, ScreensaverPage::_mqtt_event_handler, NULL);
  esp_event_handler_register(NSPM_CONFIGMANAGER_EVENT, ESP_EVENT_ANY_ID, ScreensaverPage::_nspm_config_event_handler, NULL);

  // This is the first time showing the screensaver page.
  if (ScreensaverPage::_weather_update_data_mutex == NULL) {
    esp_log_level_set("ScreensaverPage", esp_log_level_t::ESP_LOG_DEBUG); // TODO: Set from config.

    ScreensaverPage::_weather_update_data_mutex = xSemaphoreCreateMutex();

    std::string manager_address = NSPM_ConfigManager::get_manager_address();
    std::string mqtt_base_topic = "nspanel/mqttmanager_";
    mqtt_base_topic.append(manager_address);

    std::string time_topic = mqtt_base_topic;
    time_topic.append("/status/time");
    std::string date_topic = mqtt_base_topic;
    date_topic.append("/status/date");
    std::string ampm_topic = mqtt_base_topic;
    ampm_topic.append("/status/ampm");
    std::string weather_topic = mqtt_base_topic;
    weather_topic.append("/status/weather");

    while (MqttManager::subscribe(time_topic) != ESP_OK) {
      ESP_LOGE("ScreensaverPage", "Failed to subscribe to time topic for screensaver page.");
      vTaskDelay(pdMS_TO_TICKS(500));
    }

    while (MqttManager::subscribe(date_topic) != ESP_OK) {
      ESP_LOGE("ScreensaverPage", "Failed to subscribe to date topic for screensaver page.");
      vTaskDelay(pdMS_TO_TICKS(500));
    }

    while (MqttManager::subscribe(ampm_topic) != ESP_OK) {
      ESP_LOGE("ScreensaverPage", "Failed to subscribe to AM/PM topic for screensaver page.");
      vTaskDelay(pdMS_TO_TICKS(500));
    }

    while (MqttManager::subscribe(weather_topic) != ESP_OK) {
      ESP_LOGE("ScreensaverPage", "Failed to subscribe to weather topic for screensaver page.");
      vTaskDelay(pdMS_TO_TICKS(500));
    }
  }

  ScreensaverPage::_go_to_nextion_page();
  ScreensaverPage::_update_displayed_date();
  ScreensaverPage::_update_displayed_time();
  ScreensaverPage::_update_displayed_weather_data();
}

void ScreensaverPage::unshow() {
  ScreensaverPage::_currently_shown.set(false);
  MqttManager::unregister_handler(MQTT_EVENT_DATA, ScreensaverPage::_mqtt_event_handler);
  esp_event_handler_unregister(NSPM_CONFIGMANAGER_EVENT, ESP_EVENT_ANY_ID, ScreensaverPage::_nspm_config_event_handler);
}

void ScreensaverPage::_mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
  std::string topic_string = std::string(event->topic, event->topic_len);

  std::string manager_address = NSPM_ConfigManager::get_manager_address();
  std::string mqtt_base_topic = "nspanel/mqttmanager_";
  mqtt_base_topic.append(manager_address);

  std::string time_topic = mqtt_base_topic;
  time_topic.append("/status/time");
  std::string date_topic = mqtt_base_topic;
  date_topic.append("/status/date");
  std::string ampm_topic = mqtt_base_topic;
  ampm_topic.append("/status/ampm");
  std::string weather_topic = mqtt_base_topic;
  weather_topic.append("/status/weather");

  if (topic_string.compare(time_topic) == 0) {
    ScreensaverPage::_current_time.set(std::string(event->data, event->data_len));
    ScreensaverPage::_update_displayed_time();
  } else if (topic_string.compare(date_topic) == 0) {
    // We got new date, update display:
    ScreensaverPage::_current_date.set(std::string(event->data, event->data_len));
    ScreensaverPage::_update_displayed_date();
  } else if (topic_string.compare(ampm_topic) == 0) {
    // We got new AM/PM, update display:
    ScreensaverPage::_am_pm_string.set(std::string(event->data, event->data_len));
    ScreensaverPage::_update_displayed_time();
  } else if (topic_string.compare(weather_topic) == 0) {
    NSPanelWeatherUpdate *new_weather_data = nspanel_weather_update__unpack(NULL, event->data_len, (const uint8_t *)event->data);
    if (new_weather_data != NULL) {
      if (ScreensaverPage::_weather_update_data_mutex != NULL) {
        if (xSemaphoreTake(ScreensaverPage::_weather_update_data_mutex, pdMS_TO_TICKS(500)) == pdPASS) {
          if (ScreensaverPage::_weather_update_data != nullptr) {
            nspanel_weather_update__free_unpacked(ScreensaverPage::_weather_update_data, NULL);
          }
          ScreensaverPage::_weather_update_data = new_weather_data;
          xSemaphoreGive(ScreensaverPage::_weather_update_data_mutex);

          // New weather data loaded, update display.
          ScreensaverPage::_update_displayed_weather_data();
        } else {
          ESP_LOGW("ScreensaverPage", "Failed to take weather data mutex while processing new data from MQTT. Will wait for next forecast.");
          nspanel_weather_update__free_unpacked(new_weather_data, NULL);
        }
      } else {
        ESP_LOGW("ScreensaverPage", "Weather update data mutex is NULL. Will wait for next forecast.");
        nspanel_weather_update__free_unpacked(new_weather_data, NULL);
      }
    } else {
      ESP_LOGE("ScreensaverPage", "Got new weather data on topic but couldn't decode!");
    }
  }
}

void ScreensaverPage::_nspm_config_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  switch (event_id) {
  case nspm_configmanager_event::CONFIG_LOADED: { // New config loaded while showing screensaver. Update screen
    ScreensaverPage::_go_to_nextion_page();
    ScreensaverPage::_update_displayed_date();
    ScreensaverPage::_update_displayed_time();
    ScreensaverPage::_update_displayed_weather_data();
    break;
  }

  default:
    break;
  }
}

void ScreensaverPage::_update_displayed_time() {
  if (ScreensaverPage::_current_screensaver_mode.get() == NSPANEL_CONFIG__NSPANEL_SCREENSAVER_MODE__WEATHER_WITH_BACKGROUND || ScreensaverPage::_current_screensaver_mode.get() == NSPANEL_CONFIG__NSPANEL_SCREENSAVER_MODE__WEATHER_WITHOUT_BACKGROUND) {
    Nextion::set_component_text(GUI_SCREENSAVER_PAGE::label_current_time, ScreensaverPage::_current_time.get().c_str(), 250);
    Nextion::set_component_text(GUI_SCREENSAVER_PAGE::label_am_pm_name, ScreensaverPage::_am_pm_string.get().c_str(), 250);
  } else if (ScreensaverPage::_current_screensaver_mode.get() == NSPANEL_CONFIG__NSPANEL_SCREENSAVER_MODE__DATETIME_WITH_BACKGROUND || ScreensaverPage::_current_screensaver_mode.get() == NSPANEL_CONFIG__NSPANEL_SCREENSAVER_MODE__DATETIME_WITHOUT_BACKGROUND) {
    Nextion::set_component_text(GUI_SCREENSAVER_PAGE::label_screensaver_minmal_current_time, ScreensaverPage::_current_time.get().c_str(), 250);
    Nextion::set_component_text(GUI_SCREENSAVER_PAGE::label_screensaver_minmal_am_pm_name, ScreensaverPage::_am_pm_string.get().c_str(), 250);
  } else {
    ESP_LOGE("ScreensaverPage", "Unknown screensaver mode %d while processing new time from MQTT.", (int)ScreensaverPage::_current_screensaver_mode.get());
  }
}

void ScreensaverPage::_update_displayed_date() {
  if (ScreensaverPage::_current_screensaver_mode.get() == NSPANEL_CONFIG__NSPANEL_SCREENSAVER_MODE__WEATHER_WITH_BACKGROUND || ScreensaverPage::_current_screensaver_mode.get() == NSPANEL_CONFIG__NSPANEL_SCREENSAVER_MODE__WEATHER_WITHOUT_BACKGROUND) {
    Nextion::set_component_text(GUI_SCREENSAVER_PAGE::label_current_day_name, ScreensaverPage::_current_date.get().c_str(), 250);
  } else if (ScreensaverPage::_current_screensaver_mode.get() == NSPANEL_CONFIG__NSPANEL_SCREENSAVER_MODE__DATETIME_WITH_BACKGROUND || ScreensaverPage::_current_screensaver_mode.get() == NSPANEL_CONFIG__NSPANEL_SCREENSAVER_MODE__DATETIME_WITHOUT_BACKGROUND) {
    Nextion::set_component_text(GUI_SCREENSAVER_PAGE::label_screensaver_minmal_current_day_name, ScreensaverPage::_current_date.get().c_str(), 250);
  } else {
    ESP_LOGE("ScreensaverPage", "Unknown screensaver mode %d while processing new date from MQTT.", (int)ScreensaverPage::_current_screensaver_mode.get());
  }
}

void ScreensaverPage::_update_displayed_weather_data() {
  if (xSemaphoreTake(ScreensaverPage::_weather_update_data_mutex, pdMS_TO_TICKS(1000)) == pdPASS) {
    if (ScreensaverPage::_weather_update_data != nullptr) {
      Nextion::set_component_text(GUI_SCREENSAVER_PAGE::label_current_weather_icon_name, ScreensaverPage::_weather_update_data->current_weather_icon, 250);
      Nextion::set_component_text(GUI_SCREENSAVER_PAGE::label_current_temperature_name, ScreensaverPage::_weather_update_data->current_temperature_string, 250);
      Nextion::set_component_text(GUI_SCREENSAVER_PAGE::label_current_wind_name, ScreensaverPage::_weather_update_data->current_wind_string, 250);
      Nextion::set_component_text(GUI_SCREENSAVER_PAGE::label_sunrise_name, ScreensaverPage::_weather_update_data->sunrise_string, 250);
      Nextion::set_component_text(GUI_SCREENSAVER_PAGE::label_sunset_name, ScreensaverPage::_weather_update_data->sunset_string, 250);
      Nextion::set_component_text(GUI_SCREENSAVER_PAGE::label_current_max_min_temperature_name, ScreensaverPage::_weather_update_data->current_maxmin_temperature, 250);
      Nextion::set_component_text(GUI_SCREENSAVER_PAGE::label_current_rain_name, ScreensaverPage::_weather_update_data->current_precipitation_string, 250);

      for (int i = 0; i < ScreensaverPage::_weather_update_data->n_forecast_items && i < 5; i++) { // Update all available forecasts but no more than 5 as that's how many forecasts are displayed on the page
        auto item = ScreensaverPage::_weather_update_data->forecast_items[i];
        Nextion::set_component_text(GUI_SCREENSAVER_PAGE::label_forecast_day_names[i], item->display_string, 250);
        Nextion::set_component_text(GUI_SCREENSAVER_PAGE::label_forecast_day_icon_names[i], item->weather_icon, 250);
        Nextion::set_component_text(GUI_SCREENSAVER_PAGE::label_forecast_day_max_min_names[i], item->temperature_maxmin_string, 250);
        Nextion::set_component_text(GUI_SCREENSAVER_PAGE::label_forecast_day_rain_names[i], item->precipitation_string, 250);
        Nextion::set_component_text(GUI_SCREENSAVER_PAGE::label_forecast_day_wind_names[i], item->wind_string, 250);
      }
    }
    xSemaphoreGive(ScreensaverPage::_weather_update_data_mutex);
  } else {
    ESP_LOGE("ScreensaverPage", "Failed to get _weather_update_data_mutex when updating display.");
  }
}

void ScreensaverPage::_update_display_brightness() {
  Nextion::set_brightness_level(ScreensaverPage::_screensaver_brightness.get(), 1000);
}

void ScreensaverPage::_go_to_nextion_page() {
  NSPanelConfig config;
  if (NSPM_ConfigManager::get_config(&config) == ESP_OK) {
    ScreensaverPage::_current_screensaver_mode.set(config.screensaver_mode);
    ScreensaverPage::_screensaver_brightness.set(config.screensaver_dim_level);
    ScreensaverPage::_update_display_brightness();
  } else {
    ESP_LOGE("ScreensaverPage", "Failed to get NSPanel Config when showing screensaver page! Will cancel operation.");
    return;
  }

  switch (ScreensaverPage::_current_screensaver_mode.get()) {
  case NSPANEL_CONFIG__NSPANEL_SCREENSAVER_MODE__WEATHER_WITH_BACKGROUND: {
    Nextion::set_component_value(GUI_SCREENSAVER_PAGE::screensaver_background_control_variable_name, 1, 250);
    Nextion::go_to_page(GUI_SCREENSAVER_PAGE::page_name, 250);
    ScreensaverPage::_currently_shown.set(true);
    Nextion::set_component_visibility(GUI_SCREENSAVER_PAGE::label_am_pm_name_raw, config.clock_us_style, 250);
    break;
  }

  case NSPANEL_CONFIG__NSPANEL_SCREENSAVER_MODE__WEATHER_WITHOUT_BACKGROUND: {
    Nextion::set_component_value(GUI_SCREENSAVER_PAGE::screensaver_background_control_variable_name, 0, 250);
    Nextion::go_to_page(GUI_SCREENSAVER_PAGE::page_name, 250);
    ScreensaverPage::_currently_shown.set(true);
    Nextion::set_component_visibility(GUI_SCREENSAVER_PAGE::label_am_pm_name_raw, config.clock_us_style, 250);
    break;
  }

  case NSPANEL_CONFIG__NSPANEL_SCREENSAVER_MODE__DATETIME_WITH_BACKGROUND: {
    Nextion::set_component_value(GUI_SCREENSAVER_PAGE::screensaver_background_control_variable_name, 1, 250);
    Nextion::go_to_page(GUI_SCREENSAVER_PAGE::screensaver_minimal_page_name, 250);
    ScreensaverPage::_currently_shown.set(true);
    Nextion::set_component_visibility(GUI_SCREENSAVER_PAGE::label_screensaver_minmal_am_pm_name_raw, config.clock_us_style, 250);
    break;
  }

  case NSPANEL_CONFIG__NSPANEL_SCREENSAVER_MODE__DATETIME_WITHOUT_BACKGROUND: {
    Nextion::set_component_value(GUI_SCREENSAVER_PAGE::screensaver_background_control_variable_name, 0, 250);
    Nextion::go_to_page(GUI_SCREENSAVER_PAGE::screensaver_minimal_page_name, 250);
    ScreensaverPage::_currently_shown.set(true);
    Nextion::set_component_visibility(GUI_SCREENSAVER_PAGE::label_screensaver_minmal_am_pm_name_raw, config.clock_us_style, 250);
    break;

    // TODO: Implement "No screensaver"
  }

  default:
    ESP_LOGE("ScreensaverPage", "Unknown screensaver mode when showing screensaver!");
    break;
  }
}