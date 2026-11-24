#include <BackgroundManager.hpp>
#include <ConfigManager.hpp>
#include <DisplayManager.hpp>
#include <GUI_data.hpp>
// #include <HomePage.hpp>
#include <InterfaceManager.hpp>
#include <MqttManager.hpp>
#include <NSPM_ConfigManager.hpp>
#include <NSPM_ConfigManager_event.hpp>
// #include <Nextion.hpp>
#include <RoomManager.hpp>
#include <ScreensaverPage.hpp>
#include <StatusUpdateManager_events.hpp>
// #include <cJSON.h>
#include <cstdio>
#include <cstring>
#include <esp_http_client.h>
#include <esp_log.h>
#include <lvgl.h>

// ---------------------------------------------------------------------------
// LVGL user interface. Every function in here must be called with the LVGL
// lock held (the event callbacks run on the LVGL task, which holds it).
// ---------------------------------------------------------------------------
LV_FONT_DECLARE(font_screensaver_clock_86);
LV_FONT_DECLARE(font_screensaver_temperature_46);
LV_FONT_DECLARE(font_screensaver_text_18);
LV_FONT_DECLARE(font_screensaver_text_15);
LV_FONT_DECLARE(font_weather_icons_72);
LV_FONT_DECLARE(font_weather_icons_30);
LV_FONT_DECLARE(font_screensaver_icons_18);

namespace {
// The weather icon fonts hold Material Design Icons on the letters the manager sends
// (see MQTTManager weather_icons.hpp), so a weather icon string is shown as is.
// font_screensaver_icons_18 holds these icons:
constexpr const char *ICON_HOME_TEMPERATURE = "h";
constexpr const char *ICON_PRECIPITATION = "u";
constexpr const char *ICON_WIND = "w";
constexpr const char *ICON_SUNRISE = "r";
constexpr const char *ICON_SUNSET = "s";
constexpr const char *ICON_THERMOMETER = "t";

constexpr uint32_t COLOR_BACKGROUND = 0x000000;
constexpr uint32_t COLOR_FOREGROUND = 0xFFFFFF;
constexpr int32_t SCREEN_RIGHT = DisplayManager::LANDSCAPE_WIDTH - 14; // Right edge of right-aligned text
constexpr int32_t FORECAST_DAYS = 5;
constexpr int32_t FORECAST_X = 74; // Center of the first forecast column
constexpr int32_t FORECAST_SPACING = 75;
constexpr int32_t FORECAST_COLUMN_W = 74;
constexpr int32_t AM_PM_SPACE = 36; // Room kept right of the clock for AM/PM on the weather screen
constexpr int32_t CLOCK_W = 260;    // Width of the clock labels

enum class Anchor {
  LEFT,
  CENTER,
  RIGHT,
};

struct WeatherScreen {
  lv_obj_t *screen = nullptr;
  lv_obj_t *panels[3]; // Darken the background image behind the text, in "with background" mode
  lv_obj_t *weather_icon;
  lv_obj_t *temperature;
  lv_obj_t *max_min_temperature;
  lv_obj_t *inside_temperature_icon;
  lv_obj_t *inside_temperature;
  lv_obj_t *date;
  lv_obj_t *time;
  lv_obj_t *am_pm;
  lv_obj_t *precipitation;
  lv_obj_t *wind;
  lv_obj_t *sunrise;
  lv_obj_t *sunset;
  lv_obj_t *day_name[FORECAST_DAYS];
  lv_obj_t *day_icon[FORECAST_DAYS];
  lv_obj_t *day_max_min[FORECAST_DAYS];
  lv_obj_t *day_precipitation[FORECAST_DAYS];
  lv_obj_t *day_wind[FORECAST_DAYS];
} weather;

struct MinimalScreen {
  lv_obj_t *screen = nullptr;
  lv_obj_t *weather_icon;
  lv_obj_t *temperature;
  lv_obj_t *inside_temperature_icon;
  lv_obj_t *inside_temperature;
  lv_obj_t *time;
  lv_obj_t *am_pm;
  lv_obj_t *date;
} minimal;

// Shown with the backlight off when the screensaver mode is "no screensaver".
lv_obj_t *blank_screen = nullptr;

void wake_on_touch(lv_event_t *e) {
  InterfaceManager::request_screen_on();
}

lv_obj_t *create_screen() {
  lv_obj_t *screen = lv_obj_create(NULL);
  lv_obj_set_scrollable(screen, false);
  lv_obj_set_clickable(screen, true);
  lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BACKGROUND), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_add_event_cb(screen, wake_on_touch, LV_EVENT_PRESSED, nullptr);
  return screen;
}

// A label whose text is positioned at x (its left edge, center or right edge, see anchor) and y.
// For text fonts y is the top of the capital letters; for icon fonts (cap_top = false) the top of the label.
lv_obj_t *create_label(lv_obj_t *parent, const lv_font_t *font, int32_t x, int32_t y, Anchor anchor = Anchor::LEFT, int32_t width = 200, bool cap_top = true) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(COLOR_FOREGROUND), 0);
  lv_label_set_text(label, "");
  if (cap_top) {
    y -= (font->line_height - font->base_line) - font->cap_height;
  }
  if (anchor == Anchor::LEFT) {
    lv_obj_set_pos(label, x, y);
  } else {
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, anchor == Anchor::CENTER ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(label, anchor == Anchor::CENTER ? x - width / 2 : x - width, y);
  }
  return label;
}

lv_obj_t *create_icon(lv_obj_t *parent, const lv_font_t *font, const char *icon, int32_t x, int32_t y, Anchor anchor = Anchor::LEFT, int32_t width = 0) {
  lv_obj_t *label = create_label(parent, font, x, y, anchor, width, false);
  lv_label_set_text(label, icon);
  return label;
}

void build_weather_screen() {
  WeatherScreen &w = weather;
  w.screen = create_screen();

  // Panels first, so they are drawn behind the text: current weather, clock, and forecast.
  static constexpr lv_area_t panel_areas[3] = {{6, 6, 203, 150}, {209, 6, 447, 150}, {6, 157, 447, 314}};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *panel = lv_obj_create(w.screen);
    lv_obj_remove_style_all(panel);
    lv_obj_set_scrollable(panel, false);
    lv_obj_set_clickable(panel, false);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_50, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_pos(panel, panel_areas[i].x1, panel_areas[i].y1);
    lv_obj_set_size(panel, lv_area_get_width(&panel_areas[i]), lv_area_get_height(&panel_areas[i]));
    w.panels[i] = panel;
  }

  // Current weather, top left
  w.weather_icon = create_icon(w.screen, &font_weather_icons_72, "", 14, 16);
  w.temperature = create_label(w.screen, &font_screensaver_temperature_46, 107, 30);
  w.max_min_temperature = create_label(w.screen, &font_screensaver_text_18, 107, 74);

  // Inside temperature and date, top right
  w.inside_temperature_icon = create_icon(w.screen, &font_screensaver_icons_18, ICON_HOME_TEMPERATURE, 214, 9);
  w.inside_temperature = create_label(w.screen, &font_screensaver_text_18, 235, 13);
  w.date = create_label(w.screen, &font_screensaver_text_18, SCREEN_RIGHT, 13, Anchor::RIGHT);

  // Clock
  w.time = create_label(w.screen, &font_screensaver_clock_86, SCREEN_RIGHT, 58, Anchor::RIGHT, CLOCK_W);
  w.am_pm = create_label(w.screen, &font_screensaver_text_15, SCREEN_RIGHT, 104, Anchor::RIGHT, AM_PM_SPACE);

  // Current precipitation, wind, sunrise and sunset
  constexpr int32_t row_y = 127;
  create_icon(w.screen, &font_screensaver_icons_18, ICON_PRECIPITATION, 14, row_y - 4);
  w.precipitation = create_label(w.screen, &font_screensaver_text_18, 34, row_y);
  create_icon(w.screen, &font_screensaver_icons_18, ICON_WIND, 106, row_y - 4);
  w.wind = create_label(w.screen, &font_screensaver_text_18, 128, row_y);
  create_icon(w.screen, &font_screensaver_icons_18, ICON_SUNRISE, 214, row_y - 4);
  w.sunrise = create_label(w.screen, &font_screensaver_text_18, 237, row_y);
  create_icon(w.screen, &font_screensaver_icons_18, ICON_SUNSET, 330, row_y - 4);
  w.sunset = create_label(w.screen, &font_screensaver_text_18, 353, row_y);

  // Forecast: one column per day, with row icons on the left
  constexpr int32_t day_y = 178, icon_y = 196, max_min_y = 237, precipitation_y = 263, wind_y = 289;
  create_icon(w.screen, &font_screensaver_icons_18, ICON_THERMOMETER, 12, max_min_y - 4);
  create_icon(w.screen, &font_screensaver_icons_18, ICON_PRECIPITATION, 12, precipitation_y - 4);
  create_icon(w.screen, &font_screensaver_icons_18, ICON_WIND, 12, wind_y - 4);
  for (int i = 0; i < FORECAST_DAYS; i++) {
    int32_t x = FORECAST_X + i * FORECAST_SPACING;
    w.day_name[i] = create_label(w.screen, &font_screensaver_text_15, x, day_y, Anchor::CENTER, FORECAST_COLUMN_W);
    w.day_icon[i] = create_icon(w.screen, &font_weather_icons_30, "", x, icon_y, Anchor::CENTER, FORECAST_COLUMN_W);
    w.day_max_min[i] = create_label(w.screen, &font_screensaver_text_15, x, max_min_y, Anchor::CENTER, FORECAST_COLUMN_W);
    w.day_precipitation[i] = create_label(w.screen, &font_screensaver_text_15, x, precipitation_y, Anchor::CENTER, FORECAST_COLUMN_W);
    w.day_wind[i] = create_label(w.screen, &font_screensaver_text_15, x, wind_y, Anchor::CENTER, FORECAST_COLUMN_W);
  }
}

void build_minimal_screen() {
  MinimalScreen &m = minimal;
  m.screen = create_screen();

  // Outside weather, top left
  m.weather_icon = create_icon(m.screen, &font_weather_icons_30, "", 12, 6);
  m.temperature = create_label(m.screen, &font_screensaver_text_18, 48, 17);

  // Inside temperature, top right
  m.inside_temperature_icon = create_icon(m.screen, &font_screensaver_icons_18, ICON_HOME_TEMPERATURE, 383, 13);
  m.inside_temperature = create_label(m.screen, &font_screensaver_text_18, 405, 17);

  constexpr int32_t center_x = DisplayManager::LANDSCAPE_WIDTH / 2;
  m.time = create_label(m.screen, &font_screensaver_clock_86, center_x, 103, Anchor::CENTER, CLOCK_W);
  m.am_pm = create_label(m.screen, &font_screensaver_text_15, center_x + 120, 135);
  m.date = create_label(m.screen, &font_screensaver_text_18, center_x, 190, Anchor::CENTER, 260);
}

void build_screens() {
  if (blank_screen != nullptr) {
    return;
  }
  build_weather_screen();
  build_minimal_screen();
  blank_screen = create_screen();
  BackgroundManager::attach(weather.screen);
  BackgroundManager::attach(minimal.screen);
}

void set_text(lv_obj_t *label, const char *text) {
  if (strcmp(lv_label_get_text(label), text) != 0) { // Unchanged text would still redraw the label
    lv_label_set_text(label, text);
  }
}

void set_visible(lv_obj_t *obj, bool visible) {
  lv_obj_set_hidden(obj, !visible);
}

bool is_weather_mode(NSPanelConfig__NSPanelScreensaverMode mode) {
  return mode == NSPANEL_CONFIG__NSPANEL_SCREENSAVER_MODE__WEATHER_WITH_BACKGROUND || mode == NSPANEL_CONFIG__NSPANEL_SCREENSAVER_MODE__WEATHER_WITHOUT_BACKGROUND;
}

bool is_minimal_mode(NSPanelConfig__NSPanelScreensaverMode mode) {
  return mode == NSPANEL_CONFIG__NSPANEL_SCREENSAVER_MODE__DATETIME_WITH_BACKGROUND || mode == NSPANEL_CONFIG__NSPANEL_SCREENSAVER_MODE__DATETIME_WITHOUT_BACKGROUND;
}
} // namespace


void ScreensaverPage::init() {
  // Only register handlers once. init() used to run on every CONFIG_LOADED,
  // re-registering every handler and re-subscribing every topic each time.
  if (ScreensaverPage::_weather_update_data_mutex != NULL) {
    return;
  }
  esp_log_level_set("ScreensaverPage", ConfigManager::log_level);
  ScreensaverPage::_weather_update_data_mutex = xSemaphoreCreateMutex();

  MqttManager::register_handler(MQTT_EVENT_ANY, ScreensaverPage::_mqtt_event_handler, NULL);
  esp_event_handler_register(NSPM_CONFIGMANAGER_EVENT, ESP_EVENT_ANY_ID, ScreensaverPage::_nspm_config_event_handler, NULL);
  esp_event_handler_register(STATUSUPDATEMANAGER_EVENT, statusupdatemanagerevent_t::AVERAGE_TEMP_UPDATE, ScreensaverPage::_new_temperature_event, NULL);

  ScreensaverPage::_subscribe_to_mqtt_topics();
}

std::vector<uint8_t> _download_data_store;
esp_err_t HttpEventHandler(esp_http_client_event_t *event) {
  switch (event->event_id) {
  case HTTP_EVENT_ON_DATA: {
    if (!esp_http_client_is_chunked_response(event->client)) {
      _download_data_store.insert(_download_data_store.end(), (uint8_t *)event->data, (uint8_t *)event->data + event->data_len);
    } else {
      ESP_LOGE("ScreensaverPage", "Download data is chunked. Not supported!");
    }
    break;
  }

  default:
    break;
  }
  return ESP_OK;
}

void ScreensaverPage::show() {
  ESP_LOGD("ScreensaverPage", "Showing screensaver page. Currently shown? %s", ScreensaverPage::showing() ? "Yes" : "No");
  if (ScreensaverPage::showing()) {
    // Do not "show" page again when it's already showing.
    return;
  }

  if (ScreensaverPage::_weather_update_data_mutex == NULL) {
    // Page has not been initialized, do that first.
    ScreensaverPage::init();
  }

  InterfaceManager::begin_page(ScreensaverPage::unshow);

  ScreensaverPage::_show_screen_for_mode();
  ScreensaverPage::_update_displayed_date();
  ScreensaverPage::_update_displayed_time();
  ScreensaverPage::_update_displayed_temperature();

  // Weather is only rendered while the screensaver is shown, so bring it up to date now.
  // The minimal (datetime) screensaver shows the outside temperature too.
  if (ScreensaverPage::_current_screensaver_mode.get() != NSPANEL_CONFIG__NSPANEL_SCREENSAVER_MODE__NO_SCREENSAVER) {
    xTaskCreatePinnedToCore(ScreensaverPage::_task_update_displayed_weather_data, "update_weather_data", 4096, NULL, 2, NULL, 1);
  }

  RoomManager::go_to_default_room(); // Go to default room so that it is the room that is shown when the screensaver is hidden.

  // Test code for album cover follows:
  // vTaskDelay(pdMS_TO_TICKS(2000)); // Wait for screen to finish refreshing

  // _download_data_store.clear();
  // std::string album_cover_url = "http://";
  // album_cover_url.append(NSPM_ConfigManager::get_manager_address());
  // album_cover_url.append(":");
  // album_cover_url.append(std::to_string(NSPM_ConfigManager::get_manager_port()));
  // album_cover_url.append("/api/get_album_cover");
  // ESP_LOGD("ScreensaverPage", "Downloading data from %s", album_cover_url.c_str());

  // esp_http_client_handle_t client;
  // esp_http_client_config_t config = {
  //     .url = album_cover_url.c_str(),
  //     .event_handler = HttpEventHandler,
  // };

  // client = esp_http_client_init(&config);
  // // Perform the actual HTTP request to get data
  // esp_err_t err = esp_http_client_perform(client);
  // if (err == ESP_OK) {
  //   esp_http_client_cleanup(client);
  // } else {
  //   ESP_LOGE("ScreensaverPage", "Failed to download data from %s. Got error: %s. HTTP Status code: %d.", album_cover_url, esp_err_to_name(err), esp_http_client_get_status_code(client));
  //   esp_http_client_cleanup(client);
  // }

  // cJSON *json = cJSON_ParseWithLength((const char *)_download_data_store.data(), _download_data_store.size());
  // if (json != NULL) {
  //   Nextion::go_to_page("0", 5000); // Navigate to empty page
  //   vTaskDelay(pdMS_TO_TICKS(1000));
  //   Nextion::send_raw_command("cls BLACK", 5000); // Clear screen
  //   vTaskDelay(pdMS_TO_TICKS(1000));

  //   // Assume album cover is always 100x100 pixels.
  //   int x = 0;
  //   int y = 0;
  //   const cJSON *pixel = NULL;
  //   const cJSON *pixels = cJSON_GetObjectItem(json, "pixels");
  //   ESP_LOGD("ScreensaverPage", "Starting to draw album cover. Total num pixels: %d", cJSON_GetArraySize(pixels));
  //   cJSON_ArrayForEach(pixel, pixels) {
  //     Nextion::fill(50 + x, 50 + y, 1, 1, pixel->valueint, 5000);

  //     x++;
  //     if (x == 100) {
  //       x = 0;
  //       y++;
  //     }
  //   }
  // } else {
  //   ESP_LOGE("ScreensaverPage", "Failed to parse JSON data.");
  // }
  // cJSON_Delete(json);
}

void ScreensaverPage::unshow() {
  // Nothing to reset; InterfaceManager tracks whether this page is shown.
}

bool ScreensaverPage::showing() {
  return InterfaceManager::is_current_page(ScreensaverPage::unshow);
}

void ScreensaverPage::_mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id == MQTT_EVENT_DATA) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    if (event->topic_len == 0 || event->data_len == 0) {
      return; // Do not process empty messages
    }
    if (event->data_len != event->total_data_len) [[unlikely]] {
      return; // A fragment of a message larger than the MQTT buffer, e.g. a truncated forecast. MqttManager logs the drop.
    }

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

    std::string inside_temperature_sensor_state_topic = ScreensaverPage::_inside_temperature_sensor_state_topic.get();
    if (topic_string.compare(inside_temperature_sensor_state_topic) == 0) {
      // Got new temperature from MQTT, update display.
      std::string new_temperature_string = std::string(event->data, event->data_len).c_str();
      ScreensaverPage::_current_temperature.set(new_temperature_string);
      ScreensaverPage::_update_displayed_temperature();
    } else if (topic_string.compare(time_topic) == 0) {
      ScreensaverPage::_current_time = std::string(event->data, event->data_len);
      ScreensaverPage::_update_displayed_time();
    } else if (topic_string.compare(date_topic) == 0) {
      // We got new date, update display:
      ScreensaverPage::_current_date = std::string(event->data, event->data_len);
      ScreensaverPage::_update_displayed_date();
    } else if (topic_string.compare(ampm_topic) == 0) {
      // We got new AM/PM, update display:
      ScreensaverPage::_am_pm_string = std::string(event->data, event->data_len);
      ScreensaverPage::_update_displayed_time();
    } else if (topic_string.compare(weather_topic) == 0) {
      if (ScreensaverPage::_weather_update_data_mutex != NULL) {
        if (xSemaphoreTake(ScreensaverPage::_weather_update_data_mutex, pdMS_TO_TICKS(250)) == pdPASS) {
          ScreensaverPage::_weather_update_mqtt_data.clear();
          ScreensaverPage::_weather_update_mqtt_data.insert(ScreensaverPage::_weather_update_mqtt_data.end(), event->data, event->data + event->data_len);
          xSemaphoreGive(ScreensaverPage::_weather_update_data_mutex);
          // New weather data loaded. Update the display only if the screensaver is shown; otherwise show() renders it.
          if (ScreensaverPage::showing()) {
            xTaskCreatePinnedToCore(ScreensaverPage::_task_update_displayed_weather_data, "update_weather_data", 4096, NULL, 2, NULL, 1);
          }
        } else {
          ESP_LOGW("ScreensaverPage", "Failed to take weather data mutex while processing new data from MQTT. Will wait for next forecast.");

          // TaskHandle_t holder = xSemaphoreGetMutexHolder(ScreensaverPage::_weather_update_data_mutex);
          // if (holder != NULL) {
          //   TaskStatus_t status;
          //   vTaskGetInfo(/* The handle of the task being queried. */
          //                holder,
          //                /* The TaskStatus_t structure to complete with information
          //                on xTask. */
          //                &status,
          //                /* Include the stack high water mark value in the
          //                TaskStatus_t structure. */
          //                pdTRUE,
          //                /* Include the task state in the TaskStatus_t structure. */
          //                eInvalid);

          //   ESP_LOGE("ScreensaverPage", "Holding task: %s", status.pcTaskName);
          // }
        }
      } else {
        ESP_LOGW("ScreensaverPage", "Weather update data mutex is NULL. Will wait for next forecast.");
      }
    }
  }
}

void ScreensaverPage::_nspm_config_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  switch (event_id) {
  case nspm_configmanager_event::CONFIG_LOADED: { // New config loaded while showing screensaver. Update screen
    // Only touches MQTT if the manager address or inside temperature topic changed.
    ScreensaverPage::_subscribe_to_mqtt_topics();
    if (ScreensaverPage::showing()) {
      std::shared_ptr<NSPanelConfig> new_config;
      if (NSPM_ConfigManager::get_config(&new_config) == ESP_OK) [[likely]] {
        if (ScreensaverPage::_current_screensaver_mode.get() != new_config->screensaver_mode) {
          ScreensaverPage::_current_screensaver_mode.set(new_config->screensaver_mode);

          ScreensaverPage::_show_screen_for_mode();
          ScreensaverPage::_update_displayed_date();
          ScreensaverPage::_update_displayed_time();
          xTaskCreatePinnedToCore(ScreensaverPage::_task_update_displayed_weather_data, "update_weather_data", 4096, NULL, 2, NULL, 1);
        }

        if (ScreensaverPage::_screensaver_brightness != new_config->screensaver_dim_level) {
          ScreensaverPage::_update_display_brightness();
        }
      } else {
        ESP_LOGE("ScreensaverPage", "Failed to get config while processing 'new config event'. May become out of sync with manager until next config update.");
      }

      RoomManager::go_to_default_room(); // Go to default room so that it is the room that is shown when the screensaver is hidden.
    }
    break;
  }

  default:
    break;
  }
}

void ScreensaverPage::_new_temperature_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  switch (event_id) {
  case statusupdatemanagerevent_t::AVERAGE_TEMP_UPDATE: {
    // So that no inside temperature MQTT topic is configured. If it is it means that the panel is configured to get inside temperature from
    // a sensor via MQTTManger over MQTT. In that case, simply exit this function.
    if (!ScreensaverPage::_inside_temperature_sensor_state_topic.get().empty()) {
      return;
    }
    char temperature_str[16];
    snprintf(temperature_str, sizeof(temperature_str), "%.1f", *((double *)event_data));
    ScreensaverPage::_current_temperature.set(temperature_str);
    ScreensaverPage::_update_displayed_temperature();

    break;
  }

  default:
    break;
  }
}

void ScreensaverPage::_shared_ptr_weather_update_cleanup(NSPanelWeatherUpdate *data) {
  nspanel_weather_update__free_unpacked(data, NULL);
}

void ScreensaverPage::_subscribe_to_mqtt_topics() {
  // MqttManager keeps these subscribed across reconnects, so this only needs to
  // run when the manager address or inside temperature topic changes.
  std::string manager_address = NSPM_ConfigManager::get_manager_address();
  if (manager_address.empty()) {
    ESP_LOGE("ScreensaverPage", "Failed to subscribe to relevant MQTT topics as no manager address is set.");
    return;
  }

  std::string inside_temperature_sensor_mqtt_topic = ScreensaverPage::_inside_temperature_sensor_state_topic.get();
  std::shared_ptr<NSPanelConfig> config;
  if (NSPM_ConfigManager::get_config(&config) == ESP_OK) {
    inside_temperature_sensor_mqtt_topic = std::string(config->inside_temperature_sensor_mqtt_topic);
  }

  std::string old_manager_address = ScreensaverPage::_subscribed_manager_address.get();
  std::string old_inside_temperature_sensor_mqtt_topic = ScreensaverPage::_inside_temperature_sensor_state_topic.get();
  if (manager_address != old_manager_address) {
    for (const char *suffix : {"/status/time", "/status/date", "/status/ampm", "/status/weather"}) {
      if (!old_manager_address.empty()) {
        MqttManager::unsubscribe("nspanel/mqttmanager_" + old_manager_address + suffix);
      }
      std::string topic = "nspanel/mqttmanager_" + manager_address + suffix;
      if (MqttManager::subscribe(topic) != ESP_OK) {
        ESP_LOGE("ScreensaverPage", "Failed to subscribe to %s for screensaver page.", topic.c_str());
      }
    }
    ScreensaverPage::_subscribed_manager_address.set(manager_address);
  }

  // subscribe() does nothing if the topic is already subscribed, so always call it.
  if (inside_temperature_sensor_mqtt_topic != old_inside_temperature_sensor_mqtt_topic && !old_inside_temperature_sensor_mqtt_topic.empty()) {
    MqttManager::unsubscribe(old_inside_temperature_sensor_mqtt_topic);
  }
  ScreensaverPage::_inside_temperature_sensor_state_topic.set(inside_temperature_sensor_mqtt_topic);
  if (!inside_temperature_sensor_mqtt_topic.empty()) {
    ESP_LOGD("ScreensaverPage", "Subscribing to inside temperature sensor state topic: %s", inside_temperature_sensor_mqtt_topic.c_str());
    if (MqttManager::subscribe(inside_temperature_sensor_mqtt_topic) != ESP_OK) {
      ESP_LOGE("ScreensaverPage", "Failed to subscribe to inside temperature sensor state topic.");
    }
  }
}

void ScreensaverPage::_update_displayed_time() {
  std::string time = ScreensaverPage::_current_time.get();
  std::string am_pm = ScreensaverPage::_am_pm_string.get();
  DisplayManager::Lock lock;
  if (blank_screen == nullptr) {
    return;
  }
  set_text(weather.time, time.c_str());
  set_text(weather.am_pm, am_pm.c_str());
  set_text(minimal.time, time.c_str());
  set_text(minimal.am_pm, am_pm.c_str());
}

void ScreensaverPage::_update_displayed_date() {
  std::string date = ScreensaverPage::_current_date.get();
  DisplayManager::Lock lock;
  if (blank_screen == nullptr) {
    return;
  }
  set_text(weather.date, date.c_str());
  set_text(minimal.date, date.c_str());
}

void ScreensaverPage::_update_displayed_temperature() {
  std::string temperature = ScreensaverPage::_current_temperature.get();
  if (!temperature.empty()) {
    temperature.append("°");
  }
  DisplayManager::Lock lock;
  if (blank_screen == nullptr) {
    return;
  }
  set_text(weather.inside_temperature, temperature.c_str());
  set_text(minimal.inside_temperature, temperature.c_str());
}

void ScreensaverPage::_task_update_displayed_weather_data(void *param) {
  if (!ScreensaverPage::showing()) {
    // Another page took over before this task ran, e.g. at boot, where the home page follows the screensaver within
    // 80 ms. The screensaver's labels are not shown, so do not write them. show() renders the weather next time.
    ESP_LOGD("ScreensaverPage", "Screensaver no longer shown, skipping weather update.");
  } else if (_weather_update_data_mutex != NULL && xSemaphoreTake(ScreensaverPage::_weather_update_data_mutex, pdMS_TO_TICKS(1000)) == pdPASS) [[likely]] {
    if (ScreensaverPage::_weather_update_mqtt_data.size() > 0) [[likely]] {
      NSPanelWeatherUpdate *new_weather_data = nspanel_weather_update__unpack(NULL, ScreensaverPage::_weather_update_mqtt_data.size(), ScreensaverPage::_weather_update_mqtt_data.data());
      if (new_weather_data != NULL) [[likely]] {
        ESP_LOGD("ScreensaverPage", "Updating screensaver page data.");
        ScreensaverPage::_weather_update_data = std::shared_ptr<NSPanelWeatherUpdate>(new_weather_data, &ScreensaverPage::_shared_ptr_weather_update_cleanup);
        const NSPanelWeatherUpdate *data = ScreensaverPage::_weather_update_data.get();

        DisplayManager::Lock lock;
        if (blank_screen != nullptr) {
          // The minimal screensaver shows the outside temperature only.
          set_text(minimal.weather_icon, data->current_weather_icon);
          set_text(minimal.temperature, data->current_temperature_string);

          set_text(weather.weather_icon, data->current_weather_icon);
          set_text(weather.temperature, data->current_temperature_string);
          set_text(weather.max_min_temperature, data->current_maxmin_temperature);
          set_text(weather.precipitation, data->current_precipitation_string);
          set_text(weather.wind, data->current_wind_string);
          set_text(weather.sunrise, data->sunrise_string);
          set_text(weather.sunset, data->sunset_string);

          ESP_LOGD("ScreensaverPage", "Updating forecast. Forecast items: %zu", data->n_forecast_items);
          for (int i = 0; i < FORECAST_DAYS; i++) { // Show no more than 5 days, as that's how many columns the page has
            bool has_day = i < (int)data->n_forecast_items;
            const NSPanelWeatherUpdate__ForecastItem *item = has_day ? data->forecast_items[i] : nullptr;
            set_text(weather.day_name[i], has_day ? item->display_string : "");
            set_text(weather.day_icon[i], has_day ? item->weather_icon : "");
            set_text(weather.day_max_min[i], has_day ? item->temperature_maxmin_string : "");
            set_text(weather.day_precipitation[i], has_day ? item->precipitation_string : "");
            set_text(weather.day_wind[i], has_day ? item->wind_string : "");
          }
        }
        ESP_LOGD("ScreensaverPage", "Successfully updated screensaver page with new weather data.");
      } else {
        ESP_LOGE("ScreensaverPage", "Got new weather data but failed to decode it into protobuf object.");
      }
    } else {
      ESP_LOGE("ScreensaverPage", "Trying to update screensaver page but no data to decode protobuf is available.");
    }

    xSemaphoreGive(ScreensaverPage::_weather_update_data_mutex);
  } else {
    ESP_LOGE("ScreensaverPage", "Failed to get _weather_update_data_mutex when updating display.");
  }

  vTaskDelete(NULL);         // Stop this task without causing about.
  vTaskDelay(portMAX_DELAY); // Wait for task to be deleted.
}

void ScreensaverPage::_update_display_brightness() {
  std::shared_ptr<NSPanelConfig> config;
  if (NSPM_ConfigManager::get_config(&config) == ESP_OK) {
    if (ScreensaverPage::_current_screensaver_mode.get() == NSPANEL_CONFIG__NSPANEL_SCREENSAVER_MODE__NO_SCREENSAVER) {
      ESP_LOGD("ScreensaverPage", "Screensaver mode is 'no_screensaver'. Setting display brightness to 0.");
      DisplayManager::set_brightness(0); // No screensaver is to be shown, simply set brightness to 0
    } else {
      ESP_LOGD("ScreensaverPage", "Setting screensaver display brightness to %ld.", config->screensaver_dim_level);
      DisplayManager::set_brightness(config->screensaver_dim_level);
    }
    ScreensaverPage::_screensaver_brightness = config->screensaver_dim_level;
  } else {
    ESP_LOGE("ScreensaverPage", "Failed to get NSPanel Config when showing screensaver page! Will cancel operation.");
    return;
  }
}

void ScreensaverPage::_show_screen_for_mode() {
  std::shared_ptr<NSPanelConfig> config;
  if (NSPM_ConfigManager::get_config(&config) != ESP_OK) [[unlikely]] {
    ESP_LOGE("ScreensaverPage", "Failed to get NSPanel Config when showing screensaver page! Will cancel operation.");
    return;
  }
  NSPanelConfig__NSPanelScreensaverMode mode = config->screensaver_mode;
  ScreensaverPage::_current_screensaver_mode = mode;

  {
    DisplayManager::Lock lock;
    build_screens();

    // Optional parts of the screensaver
    set_visible(weather.am_pm, config->clock_us_style);
    // Not lv_obj_get_width(): LVGL only works out an object's size on its next layout pass, so for
    // the freshly built screens of the first show it is still 0 and the clock ended up off-screen.
    lv_obj_set_x(weather.time, SCREEN_RIGHT - CLOCK_W - (config->clock_us_style ? AM_PM_SPACE + 4 : 0));
    set_visible(weather.inside_temperature, config->show_screensaver_inside_temperature);
    set_visible(weather.inside_temperature_icon, config->show_screensaver_inside_temperature);
    set_visible(minimal.am_pm, config->clock_us_style);
    set_visible(minimal.inside_temperature, config->show_screensaver_inside_temperature);
    set_visible(minimal.inside_temperature_icon, config->show_screensaver_inside_temperature);
    set_visible(minimal.temperature, config->show_screensaver_outside_temperature);
    set_visible(minimal.weather_icon, config->show_screensaver_outside_temperature);

    // "With background" shows the panel's background image behind the screensaver; without, the screensaver is black.
    bool with_background = mode == NSPANEL_CONFIG__NSPANEL_SCREENSAVER_MODE__WEATHER_WITH_BACKGROUND || mode == NSPANEL_CONFIG__NSPANEL_SCREENSAVER_MODE__DATETIME_WITH_BACKGROUND;
    lv_opa_t background_opa = with_background ? LV_OPA_COVER : LV_OPA_TRANSP;
    lv_obj_set_style_bg_image_opa(weather.screen, background_opa, 0);
    lv_obj_set_style_bg_image_opa(minimal.screen, background_opa, 0);
    for (lv_obj_t *panel : weather.panels) {
      set_visible(panel, with_background);
    }

    if (is_weather_mode(mode)) {
      ESP_LOGD("ScreensaverPage", "Showing screensaver: weather");
      lv_screen_load(weather.screen);
    } else if (is_minimal_mode(mode)) {
      ESP_LOGD("ScreensaverPage", "Showing screensaver: minimal");
      lv_screen_load(minimal.screen);
    } else if (mode == NSPANEL_CONFIG__NSPANEL_SCREENSAVER_MODE__NO_SCREENSAVER) {
      ESP_LOGD("ScreensaverPage", "Showing screensaver: no screensaver");
      lv_screen_load(blank_screen);
    } else {
      ESP_LOGE("ScreensaverPage", "Unknown screensaver mode when showing screensaver!");
      lv_screen_load(minimal.screen);
    }
  }

  ScreensaverPage::_update_display_brightness();
}
