#include <stdint.h>

#pragma once
// This file contains data that is needed to communicate between the Nextion display and the ESP32.
// This includes data such as page names, component IDs and so on

// BOOTSCREEN PAGE
class GUI_LOADING_PAGE {
public:
  static inline constexpr const char *page_name = "bootscreen";
  static inline constexpr const char *component_text_name = "t_loading";
  static inline constexpr const char *component_text_ip_text = "t_ip";
};

class GUI_HOME_PAGE {
public:
  static inline constexpr const char *page_name = "home";
  static inline constexpr const char *timer_screensaver_name = "home.sleep_display";

  static inline constexpr const char *dimmer_slider_name = "home.s_brightness";
  static inline constexpr const char *color_temperature_slider_name = "home.s_kelvin";

  static inline constexpr uint8_t dimmer_slider_id = 2;
  static inline constexpr uint8_t color_temperature_slider_id = 1;

  static inline constexpr const char *button_ceiling_name = "home.b_ceiling";
  static inline constexpr const char *button_table_name = "home.b_table";

  static inline constexpr uint8_t button_ceiling_id = 22;
  static inline constexpr uint8_t button_table_id = 21;

  static inline constexpr const char *button_scenes_name = "home.b_scenes";
  static inline constexpr uint8_t button_scenes_id = 17;
  static inline constexpr uint8_t button_scenes_room_pic = 59;
  static inline constexpr uint8_t button_scenes_room_pic2 = 60;
  static inline constexpr uint8_t button_scenes_all_rooms_pic = 61;
  static inline constexpr uint8_t button_scenes_all_rooms_pic2 = 62;

  static inline constexpr uint8_t button_room_entities_id = 5;

  static inline constexpr const char *label_ceiling_name = "home.n_ceiling";
  static inline constexpr const char *label_table_name = "home.n_table";

  static inline constexpr const char *highlight_ceiling_name = "p_lockceiling";
  static inline constexpr const char *highlight_table_name = "p_locktable";

  static inline constexpr uint16_t slider_normal_color = 65535;
  static inline constexpr uint16_t slider_highlight_color = 65024;

  static inline constexpr const char *mode_label_name = "home.mode";
  static inline constexpr uint8_t button_next_mode_id = 6;

  static inline constexpr const char *room_label_name = "home.room";
  static inline constexpr uint8_t button_next_room_id = 10;
};

class GUI_SCREENSAVER_PAGE {
public:
  static inline constexpr const char *page_name = "screensaver";
  static inline constexpr const char *screensaver_background_control_variable_name = "screensaver.ssBackground";

  static inline constexpr const char *label_current_weather_icon_name = "screensaver.curTempIcon";
  static inline constexpr const char *label_current_temperature_name = "screensaver.curTemp";
  static inline constexpr const char *label_current_room_temperature_icon_name = "screensaver.roomTempIcon";
  static inline constexpr const char *label_current_room_temperature_name = "screensaver.roomTemp";
  static inline constexpr const char *label_current_max_min_temperature_name = "screensaver.curMaxmin";
  static inline constexpr const char *label_current_rain_name = "screensaver.curRain";
  static inline constexpr const char *label_current_wind_name = "screensaver.curWind";

  static inline constexpr const char *label_current_day_name = "screensaver.curDay";
  static inline constexpr const char *label_current_time = "screensaver.curTime";
  static inline constexpr const char *label_am_pm_name = "screensaver.curAMPM";
  static inline constexpr const char *label_am_pm_name_raw = "curAMPM";
  static inline constexpr const char *label_sunrise_name = "screensaver.curSunrise";
  static inline constexpr const char *label_sunset_name = "screensaver.curSunset";

  // Variable used to check if Screensaver page is loaded for the first time. Set to 1 when Nextion Screen starts.
  // Variable is used to run code in Nextion screen to hide/show background on screensaver depending on user choice in NSPanel Manager (screensaver.ssBackground).
  // After screensaver page is loaded for the first time this variable is set to 0 so the code just has to be run once.
  static inline constexpr const char *screensaver_minimal_page_name = "screensaver2";
  static inline constexpr const char *screensaver_minimal_firstview_variable_name = "screensaver2.firstview";
  static inline constexpr const char *screensaver_minimal_background_control_variable_name = "screensaver2.ssBackground";

  static inline constexpr const char *label_screensaver_minimal_current_weather_icon_name = "screensaver2.curTempIcon";
  static inline constexpr const char *label_screensaver_minimal_current_temperature_name = "screensaver2.curTemp";
  static inline constexpr const char *label_screensaver_minimal_current_room_temperature_icon_name = "screensaver2.roomTempIcon";
  static inline constexpr const char *label_screensaver_minimal_current_room_temperature_name = "screensaver2.roomTemp";

  static inline constexpr const char *label_screensaver_minimal_current_day_name = "screensaver2.curDay";
  static inline constexpr const char *label_screensaver_minimal_current_time = "screensaver2.curTime";
  static inline constexpr const char *label_screensaver_minimal_am_pm_name = "screensaver2.curAMPM";
  static inline constexpr const char *label_screensaver_minimal_am_pm_name_raw = "screensaver2.curAMPM";

  static inline constexpr const char *label_forecast_day_names[] = {
      "forDay1",
      "forDay2",
      "forDay3",
      "forDay4",
      "forDay5",
  };

  static inline constexpr const char *label_forecast_day_icon_names[] = {
      "forIcon1",
      "forIcon2",
      "forIcon3",
      "forIcon4",
      "forIcon5",
  };

  static inline constexpr const char *label_forecast_day_max_min_names[] = {
      "forMaxmin1",
      "forMaxmin2",
      "forMaxmin3",
      "forMaxmin4",
      "forMaxmin5",
  };

  static inline constexpr const char *label_forecast_day_rain_names[] = {
      "forRain1",
      "forRain2",
      "forRain3",
      "forRain4",
      "forRain5",
  };

  static inline constexpr const char *label_forecast_day_wind_names[] = {
      "forWind1",
      "forWind2",
      "forWind3",
      "forWind4",
      "forWind5",
  };
};

class GUI_DROPDOWN_PAGE {
public:
  /*
  Dropdown page has 2 rows with 3 buttons each
  Each "button" consists of 3 Nextion devices:
  1 Dual state button (val=0/1 for OFF/ON, txt=is used to choose icon in font
  2 Text Label and
  3 Hotspot.
  Hotspot is covering both label and button and is what the user is pressing.
  Press and release event is activated on hotspots.
  */
  static inline constexpr const char *page_name = "dropdown";
  static inline constexpr const char *page_header_label = "current";

  static inline constexpr const char *item1_button_name = "i1_button";
  static inline constexpr const char *item1_label_name = "i1_label";
  static inline constexpr const char *item1_hotspot_name = "i1_hotspot";
  static inline constexpr uint8_t item1_hotspot_id = 13;

  static inline constexpr const char *item2_button_name = "i2_button";
  static inline constexpr const char *item2_label_name = "i2_label";
  static inline constexpr const char *item2_hotspot_name = "i2_hotspot";
  static inline constexpr uint8_t item2_hotspot_id = 14;

  static inline constexpr const char *item3_button_name = "i3_button";
  static inline constexpr const char *item3_label_name = "i3_label";
  static inline constexpr const char *item3_hotspot_name = "i3_hotspot";
  static inline constexpr uint8_t item3_hotspot_id = 15;

  static inline constexpr const char *item4_button_name = "i4_button";
  static inline constexpr const char *item4_label_name = "i4_label";
  static inline constexpr const char *item4_hotspot_name = "i4_hotspot";
  static inline constexpr uint8_t item4_hotspot_id = 16;

  static inline constexpr const char *item5_button_name = "i5_button";
  static inline constexpr const char *item5_label_name = "i5_label";
  static inline constexpr const char *item5_hotspot_name = "i5_hotspot";
  static inline constexpr uint8_t item5_hotspot_id = 17;

  static inline constexpr const char *item6_button_name = "i6_button";
  static inline constexpr const char *item6_label_name = "i6_label";
  static inline constexpr const char *item6_hotspot_name = "i6_hotspot";
  static inline constexpr uint8_t item6_hotspot_id = 18;
};

// Struct to contain all data relevant for an item slot on en "itemsX" page.
// This is used to better be able to loop over item slots instead of copy-paste lots
// of code.
struct GUI_ITEMS_PAGE_ITEM_DATA {
  uint8_t button_id;
  const char *button_name;
  uint8_t label_id;
  const char *label_name;
};

class GUI_ITEMS_PAGE_COMMON {
public:
  // color should only be changed when using the switch icon
  // when using the scene icon color should always be white
  static inline constexpr uint16_t items_button_off_pco = 65535;  // color when off
  static inline constexpr uint16_t items_button_off_pco2 = 65535; // color when off
  static inline constexpr uint16_t items_button_on_pco = 65024;   // color when on
  static inline constexpr uint16_t items_button_on_pco2 = 65024;  // color when on

  static inline constexpr const char *page_header_label = "current";
  static inline constexpr const char *slider_save_name = "slider_save";
};

// GUI_ITEMS 4, 8 and 12 pages used to display both room items and scene items
class GUI_ITEMS4_PAGE {
public:
  static inline constexpr const char *page_name = "items4";
  static inline constexpr uint8_t slider_save_id = 9;

  // Buttons
  static inline constexpr uint8_t button_back_id = 10;
  static inline constexpr uint8_t button_previous_page_id = 11;
  static inline constexpr uint8_t button_next_page_id = 13;

  static inline constexpr GUI_ITEMS_PAGE_ITEM_DATA item_slots[] = {
      {
          .button_id = 5,
          .button_name = "i1_button",
          .label_id = 1,
          .label_name = "i1_label",
      },
      {
          .button_id = 6,
          .button_name = "i2_button",
          .label_id = 2,
          .label_name = "i2_label",
      },
      {
          .button_id = 7,
          .button_name = "i3_button",
          .label_id = 3,
          .label_name = "i3_label",
      },
      {
          .button_id = 8,
          .button_name = "i4_button",
          .label_id = 4,
          .label_name = "i4_label",
      },
  };
};

class GUI_ITEMS8_PAGE {
public:
  static inline constexpr const char *page_name = "items8";
  static inline constexpr uint8_t slider_save_id = 17;

  static inline constexpr uint8_t button_back_id = 18;
  static inline constexpr uint8_t button_previous_page_id = 19;
  static inline constexpr uint8_t button_next_page_id = 21;

  static inline constexpr GUI_ITEMS_PAGE_ITEM_DATA item_slots[] = {
      {
          .button_id = 9,
          .button_name = "i1_button",
          .label_id = 1,
          .label_name = "i1_label",
      },
      {
          .button_id = 10,
          .button_name = "i2_button",
          .label_id = 2,
          .label_name = "i2_label",
      },
      {
          .button_id = 11,
          .button_name = "i3_button",
          .label_id = 3,
          .label_name = "i3_label",
      },
      {
          .button_id = 12,
          .button_name = "i4_button",
          .label_id = 4,
          .label_name = "i4_label",
      },
      {
          .button_id = 13,
          .button_name = "i5_button",
          .label_id = 5,
          .label_name = "i5_label",
      },
      {
          .button_id = 14,
          .button_name = "i6_button",
          .label_id = 6,
          .label_name = "i6_label",
      },
      {
          .button_id = 15,
          .button_name = "i7_button",
          .label_id = 7,
          .label_name = "i7_label",
      },
      {
          .button_id = 16,
          .button_name = "i8_button",
          .label_id = 8,
          .label_name = "i8_label",
      },
  };
};

class GUI_ITEMS12_PAGE {
public:
  static inline constexpr const char *page_name = "items12";
  static inline constexpr uint8_t slider_save_id = 25;

  static inline constexpr uint8_t button_back_id = 26;
  static inline constexpr uint8_t button_previous_page_id = 27;
  static inline constexpr uint8_t button_next_page_id = 29;

  static inline constexpr GUI_ITEMS_PAGE_ITEM_DATA item_slots[] = {
      {
          .button_id = 13,
          .button_name = "i1_button",
          .label_id = 1,
          .label_name = "i1_label",
      },
      {
          .button_id = 14,
          .button_name = "i2_button",
          .label_id = 2,
          .label_name = "i2_label",
      },
      {
          .button_id = 15,
          .button_name = "i3_button",
          .label_id = 3,
          .label_name = "i3_label",
      },
      {
          .button_id = 16,
          .button_name = "i4_button",
          .label_id = 4,
          .label_name = "i4_label",
      },
      {
          .button_id = 17,
          .button_name = "i5_button",
          .label_id = 5,
          .label_name = "i5_label",
      },
      {
          .button_id = 18,
          .button_name = "i6_button",
          .label_id = 6,
          .label_name = "i6_label",
      },
      {
          .button_id = 19,
          .button_name = "i7_button",
          .label_id = 7,
          .label_name = "i7_label",
      },
      {
          .button_id = 20,
          .button_name = "i8_button",
          .label_id = 8,
          .label_name = "i8_label",
      },
      {
          .button_id = 21,
          .button_name = "i9_button",
          .label_id = 9,
          .label_name = "i9_label",
      },
      {
          .button_id = 22,
          .button_name = "i10_button",
          .label_id = 10,
          .label_name = "i10_label",
      },
      {
          .button_id = 23,
          .button_name = "i11_button",
          .label_id = 11,
          .label_name = "i11_label",
      },
      {
          .button_id = 24,
          .button_name = "i12_button",
          .label_id = 12,
          .label_name = "i12_label",
      },
  };
};

class GUI_LIGHT_CONTROL_PAGE {
public:
  static inline constexpr const uint8_t page_id = 5;
  static inline constexpr const char *page_name = "lights";

  static inline constexpr const uint8_t back_button_id = 1;
  static inline constexpr const uint8_t switch_mode_button_id = 8;
  static inline constexpr const uint8_t brightness_slider_id = 7;
  static inline constexpr const uint8_t kelvin_saturation_slider_id = 5; // Kelvin & Saturation slider depending on mode
  static inline constexpr const uint8_t hue_slider_id = 6;

  static inline constexpr const char *light_label_name = "currentLight";
  static inline constexpr const char *switch_mode_button_name = "colormode";
  static inline constexpr const char *brightness_slider_name = "brightness";
  static inline constexpr const char *kelvin_saturation_slider_name = "kelvin_sat"; // Kelvin & Saturation slider depending on mode
  static inline constexpr const char *hue_slider_name = "hue";

  static inline constexpr const uint8_t kelvin_slider_pic = 78;      // pic for "slider"
  static inline constexpr const uint8_t kelvin_slider_pic1 = 79;     // pic for "slider"
  static inline constexpr const uint8_t saturation_slider_pic = 82;  // pic for "slider"
  static inline constexpr const uint8_t saturation_slider_pic1 = 83; // pic for "slider"
  static inline constexpr const uint8_t kelvin_mode_pic = 86;        // pic for mode button
  static inline constexpr const uint8_t rgb_mode_pic = 87;           // pic for mode button
};

// Struct to contain all data relevant for a thermostat option slot on the thermostat page.
// This is used to better be able to loop over option slots instead of copy-paste lots
// of code.
struct GUI_THERMOSTAT_OPTIONS_MODE_DATA {
  uint8_t icon_id;
  const char *icon_name;
  uint8_t label_id;
  const char *label_name;
};

class GUI_THERMOSTAT_CONTROL_PAGE {
public:
  static inline constexpr const uint8_t page_id = 11;
  static inline constexpr const char *page_name = "thermostat";

  static inline constexpr const uint8_t back_button_id = 1;
  static inline constexpr const uint8_t next_button_id = 3;
  static inline constexpr const uint8_t previous_button_id = 2;
  static inline constexpr const uint8_t next_mode_button_id = 7;
  static inline constexpr const uint8_t previous_mode_button_id = 10;

  static inline constexpr const char *room_name_label_name = "current";
  static inline constexpr const char *temperature_label_name = "roomTemp";
  static inline constexpr const char *set_label_name = "setTemp";

  static inline constexpr GUI_THERMOSTAT_OPTIONS_MODE_DATA options[] = {
      // TODO: Fix data
      {
          .icon_id = 20,
          .icon_name = "set1icon",
          .label_id = 21,
          .label_name = "set1text",
      },
      {
          .icon_id = 22,
          .icon_name = "set2icon",
          .label_id = 23,
          .label_name = "set2text",
      },
      {
          .icon_id = 24,
          .icon_name = "set3icon",
          .label_id = 25,
          .label_name = "set3text",
      },
      {
          .icon_id = 26,
          .icon_name = "set4icon",
          .label_id = 27,
          .label_name = "set4text",
      },
      {
          .icon_id = 28,
          .icon_name = "set5icon",
          .label_id = 29,
          .label_name = "set5text",
      },
  };
};

// Icon glyphs in the "mdi_media_h48" font (font 17 in the HMI), used by the media player page.
//
// Material Design Icons remapped onto ASCII by build_icon_font.py, because the Nextion font
// generator drops MDI's private use code points. The letters follow the order of that script's
// shortlist, so rebuilding the font with a different shortlist moves them. Every glyph in the font
// is listed here.
//
// The generator keeps no left side bearing: every glyph is drawn flush left in a 47 px cell, so a
// centred button shows its icon 6-13 px left of centre. The mediaplayer page compensates by
// shifting each button right, which only holds for the glyphs that button was laid out for.
class GUI_MEDIA_ICONS {
public:
  // Font id as referenced by a component's "font" attribute in the HMI.
  static inline constexpr const uint8_t icon_font_id = 17;

  // Transport
  static inline constexpr const char *play = "A";
  static inline constexpr const char *pause = "B";
  static inline constexpr const char *next_track = "E";
  static inline constexpr const char *previous_track = "F";

  // Shuffle and repeat
  static inline constexpr const char *shuffle = "O";
  static inline constexpr const char *shuffle_variant = "P";
  static inline constexpr const char *shuffle_disabled = "Q";
  static inline constexpr const char *repeat = "R";
  static inline constexpr const char *repeat_once = "S";
  static inline constexpr const char *repeat_off = "T";
  static inline constexpr const char *repeat_variant = "U";

  // Volume
  static inline constexpr const char *volume_high = "V"; // unmuted
  static inline constexpr const char *volume_medium = "b";
  static inline constexpr const char *volume_low = "c";
  static inline constexpr const char *volume_mute = "X"; // muted
  static inline constexpr const char *volume_plus = "Z";
  static inline constexpr const char *volume_minus = "a";
  static inline constexpr const char *plus = "d";
  static inline constexpr const char *minus = "e";

  // Player or source
  static inline constexpr const char *speaker = "f";          // this room's player
  static inline constexpr const char *speaker_multiple = "g"; // the source and every room on it
  static inline constexpr const char *music_box_multiple = "j";
  static inline constexpr const char *input_select = "n";
};

// MEDIA PLAYER PAGE ("mediaplayer")
class GUI_MEDIA_PLAYER_CONTROL_PAGE {
public:
  static inline constexpr const char *page_name = "mediaplayer";

  // The IDs the touchable components had when this was written. MediaPlayerPage looks the real IDs up
  // by name when the page opens, as they change whenever a component is added to the page, and
  // falls back to these only if a lookup fails.
  static inline constexpr const uint8_t back_button_id = 8;
  static inline constexpr const uint8_t previous_track_button_id = 12;
  static inline constexpr const uint8_t next_track_button_id = 13;
  static inline constexpr const uint8_t play_pause_button_id = 14;
  static inline constexpr const uint8_t mute_button_id = 15;
  static inline constexpr const uint8_t source_switch_button_id = 16;
  static inline constexpr const uint8_t volume_up_button_id = 17;
  static inline constexpr const uint8_t volume_down_button_id = 18;
  static inline constexpr const uint8_t album_art_hotspot_id = 21;

  static inline constexpr const char *back_button_name = "home";
  static inline constexpr const char *album_art_hotspot_name = "h_art";
  static inline constexpr const char *title_label_name = "song";
  static inline constexpr const char *artist_label_name = "artist";
  static inline constexpr const char *previous_track_button_name = "b_prev";
  static inline constexpr const char *next_track_button_name = "b_next";
  static inline constexpr const char *play_pause_button_name = "b_play";
  static inline constexpr const char *mute_button_name = "b_vol";
  static inline constexpr const char *source_switch_button_name = "b_srcswitch";
  static inline constexpr const char *volume_up_button_name = "b_vol_up";
  static inline constexpr const char *volume_down_button_name = "b_vol_down";
  static inline constexpr const char *position_bar_name = "p_pos";
  static inline constexpr const char *volume_bar_name = "p_volume";

  // Hides volume_bar_name when it fires, then disables itself. The page hides the bar in its
  // Preinitialize event, so it starts hidden.
  static inline constexpr const char *volume_bar_timer_name = "tm_vol";

  // Button glyph colours (RGB565). The HMI makes the buttons white, and amber while pressed as on the
  // entities page. Buttons the player can't use now are set grey, with no pressed colour, and back.
  static inline constexpr uint16_t button_pco = 65535;          // white
  static inline constexpr uint16_t button_pressed_pco2 = 65024; // amber, #FFC300
  static inline constexpr uint16_t button_disabled_pco = 23243; // grey, #5A595A

  // The album art is drawn over the h_art hotspot. A hotspot draws nothing, so pressing it does
  // not repaint over the art the way a button would. MediaPlayerPage reads the hotspot's position when the
  // page opens, as each layout (US portrait, EU landscape) puts it elsewhere; these are the US
  // position, used only if that read fails.
  static inline constexpr const uint16_t album_art_x = 70;
  static inline constexpr const uint16_t album_art_y = 271;
};
