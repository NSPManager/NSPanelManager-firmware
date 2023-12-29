#include "ArduinoJson/Array/JsonArray.hpp"
#include "freertos/portmacro.h"
#include <ArduinoJson.h>
#include <InterfaceConfig.hpp>
#include <MqttLog.hpp>
#include <MqttManager.hpp>
#include <NSPMConfig.h>
#include <NSPanel.hpp>
#include <PageManager.hpp>
#include <RoomManager.hpp>
#include <ScreensaverPage.hpp>
#include <TftDefines.h>

void ScreensaverPage::attachMqttCallback() {
  MqttManager::subscribeToTopic("nspanel/status/time", &ScreensaverPage::clockMqttCallback);
  MqttManager::subscribeToTopic("nspanel/status/date", &ScreensaverPage::dateMqttCallback);
  MqttManager::subscribeToTopic("nspanel/status/weather", &ScreensaverPage::weatherMqttCallback);

  if (InterfaceConfig::clock_us_style) {
    MqttManager::subscribeToTopic("nspanel/status/ampm", &ScreensaverPage::ampmMqttCallback);
  }
}

void ScreensaverPage::show() {
  if (InterfaceConfig::screensaver_mode.compare("with_background") == 0) {
    this->_show_weather = true;
    this->_screensaver_page_name = SCREENSAVER_PAGE_NAME;
    std::string screensaver_background_variable_name = this->_screensaver_page_name;
    screensaver_background_variable_name.append(".");
    screensaver_background_variable_name.append(SCREENSAVER_BACKGROUND_CHOICE_VARIABLE_NAME);
    NSPanel::instance->setComponentVal(screensaver_background_variable_name.c_str(), 1);
    NSPanel::instance->setDimLevel(InterfaceConfig::screensaver_dim_level);
  } else if (InterfaceConfig::screensaver_mode.compare("without_background") == 0) {
    this->_show_weather = true;
    this->_screensaver_page_name = SCREENSAVER_PAGE_NAME;
    std::string screensaver_background_variable_name = this->_screensaver_page_name;
    screensaver_background_variable_name.append(".");
    screensaver_background_variable_name.append(SCREENSAVER_BACKGROUND_CHOICE_VARIABLE_NAME);
    NSPanel::instance->setComponentVal(screensaver_background_variable_name.c_str(), 0);
    NSPanel::instance->setDimLevel(InterfaceConfig::screensaver_dim_level);
  } else if (InterfaceConfig::screensaver_mode.compare("datetime_with_background") == 0) {
    this->_show_weather = false;
    this->_screensaver_page_name = SCREENSAVER_MINIMAL_PAGE_NAME;
    std::string screensaver_background_variable_name = this->_screensaver_page_name;
    screensaver_background_variable_name.append(".");
    screensaver_background_variable_name.append(SCREENSAVER_BACKGROUND_CHOICE_VARIABLE_NAME);
    NSPanel::instance->setComponentVal(screensaver_background_variable_name.c_str(), 1);
    NSPanel::instance->setDimLevel(InterfaceConfig::screensaver_dim_level);
  } else if (InterfaceConfig::screensaver_mode.compare("datetime_without_background") == 0) {
    this->_show_weather = false;
    this->_screensaver_page_name = SCREENSAVER_MINIMAL_PAGE_NAME;
    std::string screensaver_background_variable_name = this->_screensaver_page_name;
    screensaver_background_variable_name.append(".");
    screensaver_background_variable_name.append(SCREENSAVER_BACKGROUND_CHOICE_VARIABLE_NAME);
    NSPanel::instance->setComponentVal(screensaver_background_variable_name.c_str(), 0);
    NSPanel::instance->setDimLevel(InterfaceConfig::screensaver_dim_level);
  } else if (InterfaceConfig::screensaver_mode.compare("no_screensaver") == 0) {
    this->_show_weather = false;
    NSPanel::instance->setDimLevel(0);
  } else {
    LOG_ERROR("Unknown screensaver mode '", InterfaceConfig::screensaver_mode.c_str(), "'!");
  }

  this->_screensaver_time_name = this->_screensaver_page_name;
  this->_screensaver_time_name.append(".");
  this->_screensaver_time_name.append(SCREENSAVER_CURRENT_TIME_TEXT_NAME);
  this->_screensaver_date_name = this->_screensaver_page_name;
  this->_screensaver_date_name.append(".");
  this->_screensaver_date_name.append(SCREENSAVER_CURRENT_DAY_TEXT_NAME);
  this->_screensaver_temperature_name = this->_screensaver_page_name;
  this->_screensaver_temperature_name.append(".");
  this->_screensaver_temperature_name.append(SCREENSAVER_CURRENT_ROOMTEMP_TEXT_NAME);
  this->_screensaver_ampm_name = this->_screensaver_page_name;
  this->_screensaver_ampm_name.append(".");
  this->_screensaver_ampm_name.append(SCREENSAVER_CURRENT_AMPM_TEXT_NAME);

  PageManager::SetCurrentPage(this);
  NSPanel::instance->goToPage(this->_screensaver_page_name.c_str());
  MqttManager::publish(NSPMConfig::instance->mqtt_screen_state_topic, "0");
  PageManager::GetHomePage()->setCurrentMode(roomMode::room);
  RoomManager::goToRoomId(InterfaceConfig::homeScreen);

  if (InterfaceConfig::clock_us_style) {
    NSPanel::instance->setComponentVisible(SCREENSAVER_CURRENT_AMPM_TEXT_NAME, true);
  } else {
    NSPanel::instance->setComponentVisible(SCREENSAVER_CURRENT_AMPM_TEXT_NAME, false);
  }
}

void ScreensaverPage::update() {
  // Update is done though MQTT time callback
}

void ScreensaverPage::updateRoomTemp(std::string roomtemp_string) {
  NSPanel::instance->setComponentText(PageManager::GetScreensaverPage()->_screensaver_temperature_name.c_str(), roomtemp_string.c_str());
}

void ScreensaverPage::processTouchEvent(uint8_t page, uint8_t component, bool pressed) {
  LOG_DEBUG("Got touch event, component ", page, ".", component, " ", pressed ? "pressed" : "released");
  PageManager::GetHomePage()->show();
}

void ScreensaverPage::unshow() {
  NSPanel::instance->setDimLevel(InterfaceConfig::screen_dim_level);
  MqttManager::publish(NSPMConfig::instance->mqtt_screen_state_topic, "1");
}

void ScreensaverPage::clockMqttCallback(char *topic, byte *payload, unsigned int length) {
  std::string clock_string = std::string((char *)payload, length);
  NSPanel::instance->setComponentText(PageManager::GetScreensaverPage()->_screensaver_time_name.c_str(), clock_string.c_str());
}

void ScreensaverPage::ampmMqttCallback(char *topic, byte *payload, unsigned int length) {
  std::string ampm_string = std::string((char *)payload, length);
  NSPanel::instance->setComponentText(PageManager::GetScreensaverPage()->_screensaver_ampm_name.c_str(), ampm_string.c_str());
}

void ScreensaverPage::dateMqttCallback(char *topic, byte *payload, unsigned int length) {
  std::string date_string = std::string((char *)payload, length);
  NSPanel::instance->setComponentText(PageManager::GetScreensaverPage()->_screensaver_date_name.c_str(), date_string.c_str());
}

void ScreensaverPage::weatherMqttCallback(char *topic, byte *payload, unsigned int length) {
  if (!PageManager::GetScreensaverPage()->_show_weather) {
    return;
  }

  LOG_DEBUG("Received new weather data.");
  std::string payload_str = std::string((char *)payload, length);
  StaticJsonDocument<1024> json;
  DeserializationError error = deserializeJson(json, payload_str);
  if (error) {
    LOG_ERROR("Failed to serialize weather data.");
    return;
  }
  vTaskDelay(500 / portTICK_PERIOD_MS);
  JsonArray forcast = json["forcast"].as<JsonArray>();
  LOG_DEBUG("Received forcast for ", forcast.size(), " days.");

  NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_CURRENT_WEATHER_ICON_TEXT_NAME, json["icon"]);
  NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_CURRENT_TEMP_TEXT_NAME, json["temp"]);
  NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_CURRENT_WIND_TEXT_NAME, json["wind"]);

  if (forcast.size() >= 1) {
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_CURRENT_MAXMIN_TEXT_NAME, forcast[0]["maxmin"]);
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_CURRENT_RAIN_TEXT_NAME, forcast[0]["prepro"]);
  }

  if (forcast.size() >= 2) {
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_DAY1_TEXT_NAME, forcast[1]["day"]);
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_ICON1_TEXT_NAME, forcast[1]["icon"]);
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_MAXMIN1_TEXT_NAME, forcast[1]["maxmin"]);
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_RAIN1_TEXT_NAME, forcast[1]["prepro"]);
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_WIND1_TEXT_NAME, forcast[1]["wind"]);
  }

  if (forcast.size() >= 3) {
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_DAY2_TEXT_NAME, forcast[2]["day"]);
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_ICON2_TEXT_NAME, forcast[2]["icon"]);
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_MAXMIN2_TEXT_NAME, forcast[2]["maxmin"]);
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_RAIN2_TEXT_NAME, forcast[2]["prepro"]);
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_WIND2_TEXT_NAME, forcast[2]["wind"]);
  }

  if (forcast.size() >= 4) {
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_DAY3_TEXT_NAME, forcast[3]["day"]);
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_ICON3_TEXT_NAME, forcast[3]["icon"]);
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_MAXMIN3_TEXT_NAME, forcast[3]["maxmin"]);
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_RAIN3_TEXT_NAME, forcast[3]["prepro"]);
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_WIND3_TEXT_NAME, forcast[3]["wind"]);
  }

  if (forcast.size() >= 5) {
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_DAY4_TEXT_NAME, forcast[4]["day"]);
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_ICON4_TEXT_NAME, forcast[4]["icon"]);
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_MAXMIN4_TEXT_NAME, forcast[4]["maxmin"]);
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_RAIN4_TEXT_NAME, forcast[4]["prepro"]);
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_WIND4_TEXT_NAME, forcast[4]["wind"]);
  }

  if (forcast.size() >= 6) {
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_DAY5_TEXT_NAME, forcast[5]["day"]);
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_ICON5_TEXT_NAME, forcast[5]["icon"]);
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_MAXMIN5_TEXT_NAME, forcast[5]["maxmin"]);
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_RAIN5_TEXT_NAME, forcast[5]["prepro"]);
    NSPanel::instance->setComponentText(SCREENSAVER_PAGE_NAME "." SCREENSAVER_FORECAST_WIND5_TEXT_NAME, forcast[5]["wind"]);
  }
}