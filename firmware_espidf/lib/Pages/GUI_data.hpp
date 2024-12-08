#include <stdint.h>

#pragma once
// This file contains data that is needed to communicate between the Nextion display and the ESP32.
// This includes data such as page names, component IDs and so on

// BOOTSCREEN PAGE
class GUI_LOADING_PAGE {
public:
  static inline const char *page_name = "bootscreen";
  static inline const char *component_text_name = "t_loading";
  static inline const char *component_text_ip_text = "t_ip";
};

class GUI_HOME_PAGE {
public:
  static inline const char *page_name = "home";
  static inline const char *timer_screensaver_name = "home.sleep_display";

  static inline const char *dimmer_slider_name = "home.s_brightness";
  static inline const char *color_temperature_slider_name = "home.s_kelvin";

  static inline const uint8_t dimmer_slider_id = 2;
  static inline const uint8_t color_temperature_slider_id = 1;

  static inline const char *button_ceiling_name = "home.b_ceiling";
  static inline const char *button_table_name = "home.b_table";

  static inline const uint8_t button_ceiling_id = 22;
  static inline const uint8_t button_table_id = 21;

  static inline const char *button_scenes_name = "home.b_scenes";
  static inline const uint8_t button_scenes_room_pic = 59;
  static inline const uint8_t button_scenes_room_pic2 = 50;
  static inline const uint8_t button_scenes_all_rooms_pic = 61;
  static inline const uint8_t button_scenes_all_rooms_pic2 = 62;

  static inline const uint8_t button_room_entities_id = 5;

  static inline const char *label_ceiling_name = "home.n_ceiling";
  static inline const char *label_table_name = "home.n_table";

  static inline const char *highlight_ceiling_name = "p_lockceiling";
  static inline const char *highlight_table_name = "p_locktable";

  static inline const uint16_t slider_normal_color = 65535;
  static inline const uint16_t slider_highlight_color = 65024;

  static inline const char *mode_label_name = "home.mode";
  static inline const uint8_t button_next_mode_id = 6;

  static inline const char *room_label_name = "home.room";
  static inline const uint8_t button_next_room_id = 10;
};

class GUI_SCREENSAVER_PAGE {
public:
  static inline const char *page_name = "screensaver";
  static inline const char *screensaver_background_control_variable_name = "screensaver.ssBackground";

  static inline const char *label_current_weather_icon_name = "screensaver.curIcon";
  static inline const char *label_current_temperature_name = "screensaver.curTemp";
  static inline const char *label_current_max_min_temperature_name = "screensaver.curMaxmin";
  static inline const char *label_current_rain_name = "screensaver.curRain";
  static inline const char *label_current_wind_name = "screensaver.curWind";
  static inline const char *label_current_room_temperature_name = "screensaver.curRoomtemp";
  static inline const char *label_current_room_temperature_icon_name = "screensaver.t7"; // TODO: Needed/Used anywhere?

  static inline const char *label_current_day_name = "screensaver.curDay";
  static inline const char *label_current_time = "screensaver.curTime";
  static inline const char *label_am_pm_name = "screensaver.curAMPM";
  static inline const char *label_am_pm_name_raw = "curAMPM";
  static inline const char *label_sunrise_name = "screensaver.curSunrise";
  static inline const char *label_sunset_name = "screensaver.curSunset";

  // Variable used to check if Screensaver page is loaded for the first time. Set to 1 when Nextion Screen starts.
  // Variable is used to run code in Nextion screen to hide/show background on screensaver depending on user choice in NSPanel Manager (screensaver.ssBackground).
  // After screensaver page is loaded for the first time this variable is set to 0 so the code just has to be run once.
  static inline const char *screensaver_minimal_page_name = "screensaver2";
  static inline const char *screensaver_minimal_firstview_variable_name = "screensaver2.firstview";
  static inline const char *screensaver_minimal_background_control_variable_name = "screensaver2.ssBackground";

  static inline const char *label_screensaver_minimal_current_room_temperature_name = "screensaver2.curRoomtemp";
  static inline const char *label_screensaver_minimal_current_room_temperature_icon_name = "screensaver.t7"; // TODO: Needed/Used anywhere?
  static inline const char *label_screensaver_minmal_current_day_name = "screensaver.curDay";
  static inline const char *label_screensaver_minmal_current_time = "screensaver.curTime";
  static inline const char *label_screensaver_minmal_am_pm_name = "screensaver.curAMPM";
  static inline const char *label_screensaver_minmal_am_pm_name_raw = "curAMPM";

  static inline const char *label_forecast_day_names[] = {
      "forDay1",
      "forDay2",
      "forDay3",
      "forDay4",
      "forDay5",
  };

  static inline const char *label_forecast_day_icon_names[] = {
      "forIcon1",
      "forIcon2",
      "forIcon3",
      "forIcon4",
      "forIcon5",
  };

  static inline const char *label_forecast_day_max_min_names[] = {
      "forMaxmin1",
      "forMaxmin2",
      "forMaxmin3",
      "forMaxmin4",
      "forMaxmin5",
  };

  static inline const char *label_forecast_day_rain_names[] = {
      "forRain1",
      "forRain2",
      "forRain3",
      "forRain4",
      "forRain5",
  };

  static inline const char *label_forecast_day_wind_names[] = {
      "forWind1",
      "forWind2",
      "forWind3",
      "forWind4",
      "forWind5",
  };
};