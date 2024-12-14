#include <stdint.h>

#pragma once
// This file contains data that is needed to communicate between the Nextion display and the ESP32.
// This includes data such as page names, component IDs and so on

// BOOTSCREEN PAGE
class GUI_LOADING_PAGE {
public:
  static inline constexpr char *page_name = "bootscreen";
  static inline constexpr char *component_text_name = "t_loading";
  static inline constexpr char *component_text_ip_text = "t_ip";
};

class GUI_HOME_PAGE {
public:
  static inline constexpr char *page_name = "home";
  static inline constexpr char *timer_screensaver_name = "home.sleep_display";

  static inline constexpr char *dimmer_slider_name = "home.s_brightness";
  static inline constexpr char *color_temperature_slider_name = "home.s_kelvin";

  static inline constexpr uint8_t dimmer_slider_id = 2;
  static inline constexpr uint8_t color_temperature_slider_id = 1;

  static inline constexpr char *button_ceiling_name = "home.b_ceiling";
  static inline constexpr char *button_table_name = "home.b_table";

  static inline constexpr uint8_t button_ceiling_id = 22;
  static inline constexpr uint8_t button_table_id = 21;

  static inline constexpr char *button_scenes_name = "home.b_scenes";
  static inline constexpr uint8_t button_scenes_room_pic = 59;
  static inline constexpr uint8_t button_scenes_room_pic2 = 50;
  static inline constexpr uint8_t button_scenes_all_rooms_pic = 61;
  static inline constexpr uint8_t button_scenes_all_rooms_pic2 = 62;

  static inline constexpr uint8_t button_room_entities_id = 5;

  static inline constexpr char *label_ceiling_name = "home.n_ceiling";
  static inline constexpr char *label_table_name = "home.n_table";

  static inline constexpr char *highlight_ceiling_name = "p_lockceiling";
  static inline constexpr char *highlight_table_name = "p_locktable";

  static inline constexpr uint16_t slider_normal_color = 65535;
  static inline constexpr uint16_t slider_highlight_color = 65024;

  static inline constexpr char *mode_label_name = "home.mode";
  static inline constexpr uint8_t button_next_mode_id = 6;

  static inline constexpr char *room_label_name = "home.room";
  static inline constexpr uint8_t button_next_room_id = 10;
};

class GUI_SCREENSAVER_PAGE {
public:
  static inline constexpr char *page_name = "screensaver";
  static inline constexpr char *screensaver_background_control_variable_name = "screensaver.ssBackground";

  static inline constexpr char *label_current_weather_icon_name = "screensaver.curIcon";
  static inline constexpr char *label_current_temperature_name = "screensaver.curTemp";
  static inline constexpr char *label_current_max_min_temperature_name = "screensaver.curMaxmin";
  static inline constexpr char *label_current_rain_name = "screensaver.curRain";
  static inline constexpr char *label_current_wind_name = "screensaver.curWind";
  static inline constexpr char *label_current_room_temperature_name = "screensaver.curRoomtemp";
  static inline constexpr char *label_current_room_temperature_icon_name = "screensaver.t7"; // TODO: Needed/Used anywhere?

  static inline constexpr char *label_current_day_name = "screensaver.curDay";
  static inline constexpr char *label_current_time = "screensaver.curTime";
  static inline constexpr char *label_am_pm_name = "screensaver.curAMPM";
  static inline constexpr char *label_am_pm_name_raw = "curAMPM";
  static inline constexpr char *label_sunrise_name = "screensaver.curSunrise";
  static inline constexpr char *label_sunset_name = "screensaver.curSunset";

  // Variable used to check if Screensaver page is loaded for the first time. Set to 1 when Nextion Screen starts.
  // Variable is used to run code in Nextion screen to hide/show background on screensaver depending on user choice in NSPanel Manager (screensaver.ssBackground).
  // After screensaver page is loaded for the first time this variable is set to 0 so the code just has to be run once.
  static inline constexpr char *screensaver_minimal_page_name = "screensaver2";
  static inline constexpr char *screensaver_minimal_firstview_variable_name = "screensaver2.firstview";
  static inline constexpr char *screensaver_minimal_background_control_variable_name = "screensaver2.ssBackground";

  static inline constexpr char *label_screensaver_minimal_current_room_temperature_name = "screensaver2.curRoomtemp";
  static inline constexpr char *label_screensaver_minimal_current_room_temperature_icon_name = "screensaver.t7"; // TODO: Needed/Used anywhere?
  static inline constexpr char *label_screensaver_minmal_current_day_name = "screensaver.curDay";
  static inline constexpr char *label_screensaver_minmal_current_time = "screensaver.curTime";
  static inline constexpr char *label_screensaver_minmal_am_pm_name = "screensaver.curAMPM";
  static inline constexpr char *label_screensaver_minmal_am_pm_name_raw = "curAMPM";

  static inline constexpr char *label_forecast_day_names[] = {
      "forDay1",
      "forDay2",
      "forDay3",
      "forDay4",
      "forDay5",
  };

  static inline constexpr char *label_forecast_day_icon_names[] = {
      "forIcon1",
      "forIcon2",
      "forIcon3",
      "forIcon4",
      "forIcon5",
  };

  static inline constexpr char *label_forecast_day_max_min_names[] = {
      "forMaxmin1",
      "forMaxmin2",
      "forMaxmin3",
      "forMaxmin4",
      "forMaxmin5",
  };

  static inline constexpr char *label_forecast_day_rain_names[] = {
      "forRain1",
      "forRain2",
      "forRain3",
      "forRain4",
      "forRain5",
  };

  static inline constexpr char *label_forecast_day_wind_names[] = {
      "forWind1",
      "forWind2",
      "forWind3",
      "forWind4",
      "forWind5",
  };
};

class GUI_DROPDOWN_PAGE {
public:
  static inline const char *page_name = "items4";
  static inline const char *label_item1 = "items4";
  static inline const char *button_item1 = "items4";
  static inline const char *hotspot_item1 = "items4"; //above button
  //static inline const uint8_t dimmer_slider_id = 2;
  //static inline const char *component_text_name = "t_loading";

};

class GUI_ITEMS4_PAGE {
public:
  static inline const char *page_name = "items4";
  static inline const uint8_t slider_save_id = 9;
  static inline const char *slider_save_name = "slider_save";
  
  static inline const uint8_t item1_id = 1;
  static inline const char *item1_label_name = "i1_label";
  static inline const char *item1_button_name = "i1_button";  
  static inline const uint8_t item2_id = 2;
  static inline const char *item2_label_name = "i2_label";
  static inline const char *item2_button_name = "i2_button";
  static inline const uint8_t item3_id = 3;
  static inline const char *item3_label_name = "i3_label";
  static inline const char *item3_button_name = "i3_button";
  static inline const uint8_t item4_id = 4;
  static inline const char *item4_label_name = "i4_label";
  static inline const char *item4_button_name = "i4_button";

};

class GUI_ITEMS8_PAGE {
public:
  static inline const char *page_name = "items8";
  static inline const uint8_t slider_save_id = 17;
  static inline const char *slider_save_name = "slider_save";
  
  static inline const uint8_t item1_id = 1;
  static inline const char *item1_label_name = "i1_label";
  static inline const char *item1_button_name = "i1_button";  
  static inline const uint8_t item2_id = 2;
  static inline const char *item2_label_name = "i2_label";
  static inline const char *item2_button_name = "i2_button";
  static inline const uint8_t item3_id = 3;
  static inline const char *item3_label_name = "i3_label";
  static inline const char *item3_button_name = "i3_button";
  static inline const uint8_t item4_id = 4;
  static inline const char *item4_label_name = "i4_label";
  static inline const char *item4_button_name = "i4_button";
  static inline const uint8_t item5_id = 5;
  static inline const char *item5_label_name = "i5_label";
  static inline const char *item5_button_name = "i5_button";
  static inline const uint8_t item6_id = 6;
  static inline const char *item6_label_name = "i6_label";
  static inline const char *item6_button_name = "i6_button";
  static inline const uint8_t item7_id = 7;
  static inline const char *item7_label_name = "i7_label";
  static inline const char *item7_button_name = "i7_button";
  static inline const uint8_t item8_id = 8;
  static inline const char *item8_label_name = "i8_label";
  static inline const char *item8_button_name = "i8_button";
};

class GUI_ITEMS12_PAGE {
public:
  static inline const char *page_name = "items12";
  static inline const uint8_t slider_save_id = 30;
  static inline const char *slider_save_name = "slider_save";
  
  static inline const uint8_t item1_id = 1;
  static inline const char *item1_label_name = "i1_label";
  static inline const char *item1_button_name = "i1_button";  
  static inline const uint8_t item2_id = 2;
  static inline const char *item2_label_name = "i2_label";
  static inline const char *item2_button_name = "i2_button";
  static inline const uint8_t item3_id = 3;
  static inline const char *item3_label_name = "i3_label";
  static inline const char *item3_button_name = "i3_button";
  static inline const uint8_t item4_id = 4;
  static inline const char *item4_label_name = "i4_label";
  static inline const char *item4_button_name = "i4_button";
  static inline const uint8_t item5_id = 5;
  static inline const char *item5_label_name = "i5_label";
  static inline const char *item5_button_name = "i5_button";
  static inline const uint8_t item6_id = 6;
  static inline const char *item6_label_name = "i6_label";
  static inline const char *item6_button_name = "i6_button";
  static inline const uint8_t item7_id = 7;
  static inline const char *item7_label_name = "i7_label";
  static inline const char *item7_button_name = "i7_button";
  static inline const uint8_t item8_id = 8;
  static inline const char *item8_label_name = "i8_label";
  static inline const char *item8_button_name = "i8_button";
  static inline const uint9_t item9_id = 9;
  static inline const char *item9_label_name = "i9_label";
  static inline const char *item9_button_name = "i9_button";
  static inline const uint10_t item10_id = 10;
  static inline const char *item10_label_name = "i10_label";
  static inline const char *item10_button_name = "i10_button";
  static inline const uint11_t item11_id = 11;
  static inline const char *item11_label_name = "i11_label";
  static inline const char *item11_button_name = "i11_button";
  static inline const uint12_t item12_id = 12;
  static inline const char *item12_label_name = "i12_label";
  static inline const char *item12_button_name = "i12_button";
};
