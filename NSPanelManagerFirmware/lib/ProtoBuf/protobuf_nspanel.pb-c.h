/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: protobuf_nspanel.proto */

#ifndef PROTOBUF_C_protobuf_5fnspanel_2eproto__INCLUDED
#define PROTOBUF_C_protobuf_5fnspanel_2eproto__INCLUDED

#include <protobuf-c/protobuf-c.h>

PROTOBUF_C__BEGIN_DECLS

#if PROTOBUF_C_VERSION_NUMBER < 1003000
# error This file was generated by a newer version of protoc-c which is incompatible with your libprotobuf-c headers. Please update your headers.
#elif 1004001 < PROTOBUF_C_MIN_COMPILER_VERSION
# error This file was generated by an older version of protoc-c which is incompatible with your libprotobuf-c headers. Please regenerate this file with a newer version of protoc-c.
#endif


typedef struct NSPanelWarning NSPanelWarning;
typedef struct NSPanelStatusReport NSPanelStatusReport;
typedef struct NSPanelLightStatus NSPanelLightStatus;
typedef struct NSPanelRoomStatus NSPanelRoomStatus;
typedef struct NSPanelWeatherUpdate NSPanelWeatherUpdate;
typedef struct NSPanelWeatherUpdate__ForecastItem NSPanelWeatherUpdate__ForecastItem;
typedef struct NSPanelMQTTManagerCommand NSPanelMQTTManagerCommand;
typedef struct NSPanelMQTTManagerCommand__FirstPageTurnLightOn NSPanelMQTTManagerCommand__FirstPageTurnLightOn;
typedef struct NSPanelMQTTManagerCommand__FirstPageTurnLightOff NSPanelMQTTManagerCommand__FirstPageTurnLightOff;
typedef struct NSPanelMQTTManagerCommand__LightCommand NSPanelMQTTManagerCommand__LightCommand;


/* --- enums --- */

typedef enum _NSPanelStatusReport__State {
  NSPANEL_STATUS_REPORT__STATE__ONLINE = 0,
  NSPANEL_STATUS_REPORT__STATE__OFFLINE = 1,
  NSPANEL_STATUS_REPORT__STATE__UPDATING_TFT = 2,
  NSPANEL_STATUS_REPORT__STATE__UPDATING_FIRMWARE = 3,
  NSPANEL_STATUS_REPORT__STATE__UPDATING_LITTLEFS = 4
    PROTOBUF_C__FORCE_ENUM_TO_BE_INT_SIZE(NSPANEL_STATUS_REPORT__STATE)
} NSPanelStatusReport__State;
typedef enum _NSPanelMQTTManagerCommand__AffectLightsOptions {
  NSPANEL_MQTTMANAGER_COMMAND__AFFECT_LIGHTS_OPTIONS__ALL = 0,
  NSPANEL_MQTTMANAGER_COMMAND__AFFECT_LIGHTS_OPTIONS__TABLE_LIGHTS = 1,
  NSPANEL_MQTTMANAGER_COMMAND__AFFECT_LIGHTS_OPTIONS__CEILING_LIGHTS = 2
    PROTOBUF_C__FORCE_ENUM_TO_BE_INT_SIZE(NSPANEL_MQTTMANAGER_COMMAND__AFFECT_LIGHTS_OPTIONS)
} NSPanelMQTTManagerCommand__AffectLightsOptions;
typedef enum _NSPanelWarningLevel {
  NSPANEL_WARNING_LEVEL__CRITICAL = 0,
  NSPANEL_WARNING_LEVEL__ERROR = 1,
  NSPANEL_WARNING_LEVEL__WARNING = 2,
  NSPANEL_WARNING_LEVEL__INFO = 3,
  NSPANEL_WARNING_LEVEL__DEBUG = 4,
  NSPANEL_WARNING_LEVEL__TRACE = 5
    PROTOBUF_C__FORCE_ENUM_TO_BE_INT_SIZE(NSPANEL_WARNING_LEVEL)
} NSPanelWarningLevel;

/* --- messages --- */

struct  NSPanelWarning
{
  ProtobufCMessage base;
  NSPanelWarningLevel level;
  char *text;
};
#define NSPANEL_WARNING__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&nspanel_warning__descriptor) \
    , NSPANEL_WARNING_LEVEL__CRITICAL, (char *)protobuf_c_empty_string }


struct  NSPanelStatusReport
{
  ProtobufCMessage base;
  NSPanelStatusReport__State nspanel_state;
  int32_t update_progress;
  int32_t rssi;
  int32_t heap_used_pct;
  char *mac_address;
  char *temperature;
  char *ip_address;
  size_t n_warnings;
  NSPanelWarning **warnings;
};
#define NSPANEL_STATUS_REPORT__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&nspanel_status_report__descriptor) \
    , NSPANEL_STATUS_REPORT__STATE__ONLINE, 0, 0, 0, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, 0,NULL }


struct  NSPanelLightStatus
{
  ProtobufCMessage base;
  int32_t id;
  char *name;
  protobuf_c_boolean can_dim;
  protobuf_c_boolean can_color_temperature;
  protobuf_c_boolean can_rgb;
  int32_t light_level;
  int32_t color_temp;
  int32_t hue;
  int32_t saturation;
  int32_t room_view_position;
};
#define NSPANEL_LIGHT_STATUS__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&nspanel_light_status__descriptor) \
    , 0, (char *)protobuf_c_empty_string, 0, 0, 0, 0, 0, 0, 0, 0 }


struct  NSPanelRoomStatus
{
  ProtobufCMessage base;
  int32_t id;
  char *name;
  int32_t average_dim_level;
  int32_t ceiling_lights_dim_level;
  int32_t table_lights_dim_level;
  int32_t average_color_temperature;
  int32_t ceiling_lights_color_temperature_value;
  int32_t table_lights_color_temperature_value;
  size_t n_lights;
  NSPanelLightStatus **lights;
};
#define NSPANEL_ROOM_STATUS__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&nspanel_room_status__descriptor) \
    , 0, (char *)protobuf_c_empty_string, 0, 0, 0, 0, 0, 0, 0,NULL }


struct  NSPanelWeatherUpdate__ForecastItem
{
  ProtobufCMessage base;
  char *weather_icon;
  char *precipitation_string;
  char *temperature_maxmin_string;
  char *wind_string;
  char *display_string;
};
#define NSPANEL_WEATHER_UPDATE__FORECAST_ITEM__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&nspanel_weather_update__forecast_item__descriptor) \
    , (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string }


struct  NSPanelWeatherUpdate
{
  ProtobufCMessage base;
  size_t n_forecast_items;
  NSPanelWeatherUpdate__ForecastItem **forecast_items;
  char *current_weather_icon;
  char *current_temperature_string;
  char *current_maxmin_temperature;
  char *current_wind_string;
  char *sunrise_string;
  char *sunset_string;
  char *current_precipitation_string;
};
#define NSPANEL_WEATHER_UPDATE__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&nspanel_weather_update__descriptor) \
    , 0,NULL, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string }


struct  NSPanelMQTTManagerCommand__FirstPageTurnLightOn
{
  ProtobufCMessage base;
  NSPanelMQTTManagerCommand__AffectLightsOptions affect_lights;
  int32_t brightness_slider_value;
  int32_t kelvin_slider_value;
  int32_t selected_room;
  protobuf_c_boolean global;
  protobuf_c_boolean has_brightness_value;
  protobuf_c_boolean has_kelvin_value;
};
#define NSPANEL_MQTTMANAGER_COMMAND__FIRST_PAGE_TURN_LIGHT_ON__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&nspanel_mqttmanager_command__first_page_turn_light_on__descriptor) \
    , NSPANEL_MQTTMANAGER_COMMAND__AFFECT_LIGHTS_OPTIONS__ALL, 0, 0, 0, 0, 0, 0 }


struct  NSPanelMQTTManagerCommand__FirstPageTurnLightOff
{
  ProtobufCMessage base;
  NSPanelMQTTManagerCommand__AffectLightsOptions affect_lights;
};
#define NSPANEL_MQTTMANAGER_COMMAND__FIRST_PAGE_TURN_LIGHT_OFF__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&nspanel_mqttmanager_command__first_page_turn_light_off__descriptor) \
    , NSPANEL_MQTTMANAGER_COMMAND__AFFECT_LIGHTS_OPTIONS__ALL }


/*
 * TODO: Once protobuf-c-compiler gets updated, perhaps it's possible to build protobuf C files
 * with optional arguments.
 */
struct  NSPanelMQTTManagerCommand__LightCommand
{
  ProtobufCMessage base;
  size_t n_light_ids;
  int32_t *light_ids;
  protobuf_c_boolean has_brightness;
  int32_t brightness;
  protobuf_c_boolean has_color_temperature;
  int32_t color_temperature;
  protobuf_c_boolean has_hue;
  int32_t hue;
  protobuf_c_boolean has_saturation;
  int32_t saturation;
};
#define NSPANEL_MQTTMANAGER_COMMAND__LIGHT_COMMAND__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&nspanel_mqttmanager_command__light_command__descriptor) \
    , 0,NULL, 0, 0, 0, 0, 0, 0, 0, 0 }


typedef enum {
  NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA__NOT_SET = 0,
  NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_FIRST_PAGE_TURN_ON = 1,
  NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_FIRST_PAGE_TURN_OFF = 2,
  NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_LIGHT_COMMAND = 3
    PROTOBUF_C__FORCE_ENUM_TO_BE_INT_SIZE(NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA__CASE)
} NSPanelMQTTManagerCommand__CommandDataCase;

/*
 * Command send from NSPanel to MQTTManager
 */
struct  NSPanelMQTTManagerCommand
{
  ProtobufCMessage base;
  NSPanelMQTTManagerCommand__CommandDataCase command_data_case;
  union {
    NSPanelMQTTManagerCommand__FirstPageTurnLightOn *first_page_turn_on;
    NSPanelMQTTManagerCommand__FirstPageTurnLightOff *first_page_turn_off;
    NSPanelMQTTManagerCommand__LightCommand *light_command;
  };
};
#define NSPANEL_MQTTMANAGER_COMMAND__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&nspanel_mqttmanager_command__descriptor) \
    , NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA__NOT_SET, {0} }


/* NSPanelWarning methods */
void   nspanel_warning__init
                     (NSPanelWarning         *message);
size_t nspanel_warning__get_packed_size
                     (const NSPanelWarning   *message);
size_t nspanel_warning__pack
                     (const NSPanelWarning   *message,
                      uint8_t             *out);
size_t nspanel_warning__pack_to_buffer
                     (const NSPanelWarning   *message,
                      ProtobufCBuffer     *buffer);
NSPanelWarning *
       nspanel_warning__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   nspanel_warning__free_unpacked
                     (NSPanelWarning *message,
                      ProtobufCAllocator *allocator);
/* NSPanelStatusReport methods */
void   nspanel_status_report__init
                     (NSPanelStatusReport         *message);
size_t nspanel_status_report__get_packed_size
                     (const NSPanelStatusReport   *message);
size_t nspanel_status_report__pack
                     (const NSPanelStatusReport   *message,
                      uint8_t             *out);
size_t nspanel_status_report__pack_to_buffer
                     (const NSPanelStatusReport   *message,
                      ProtobufCBuffer     *buffer);
NSPanelStatusReport *
       nspanel_status_report__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   nspanel_status_report__free_unpacked
                     (NSPanelStatusReport *message,
                      ProtobufCAllocator *allocator);
/* NSPanelLightStatus methods */
void   nspanel_light_status__init
                     (NSPanelLightStatus         *message);
size_t nspanel_light_status__get_packed_size
                     (const NSPanelLightStatus   *message);
size_t nspanel_light_status__pack
                     (const NSPanelLightStatus   *message,
                      uint8_t             *out);
size_t nspanel_light_status__pack_to_buffer
                     (const NSPanelLightStatus   *message,
                      ProtobufCBuffer     *buffer);
NSPanelLightStatus *
       nspanel_light_status__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   nspanel_light_status__free_unpacked
                     (NSPanelLightStatus *message,
                      ProtobufCAllocator *allocator);
/* NSPanelRoomStatus methods */
void   nspanel_room_status__init
                     (NSPanelRoomStatus         *message);
size_t nspanel_room_status__get_packed_size
                     (const NSPanelRoomStatus   *message);
size_t nspanel_room_status__pack
                     (const NSPanelRoomStatus   *message,
                      uint8_t             *out);
size_t nspanel_room_status__pack_to_buffer
                     (const NSPanelRoomStatus   *message,
                      ProtobufCBuffer     *buffer);
NSPanelRoomStatus *
       nspanel_room_status__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   nspanel_room_status__free_unpacked
                     (NSPanelRoomStatus *message,
                      ProtobufCAllocator *allocator);
/* NSPanelWeatherUpdate__ForecastItem methods */
void   nspanel_weather_update__forecast_item__init
                     (NSPanelWeatherUpdate__ForecastItem         *message);
/* NSPanelWeatherUpdate methods */
void   nspanel_weather_update__init
                     (NSPanelWeatherUpdate         *message);
size_t nspanel_weather_update__get_packed_size
                     (const NSPanelWeatherUpdate   *message);
size_t nspanel_weather_update__pack
                     (const NSPanelWeatherUpdate   *message,
                      uint8_t             *out);
size_t nspanel_weather_update__pack_to_buffer
                     (const NSPanelWeatherUpdate   *message,
                      ProtobufCBuffer     *buffer);
NSPanelWeatherUpdate *
       nspanel_weather_update__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   nspanel_weather_update__free_unpacked
                     (NSPanelWeatherUpdate *message,
                      ProtobufCAllocator *allocator);
/* NSPanelMQTTManagerCommand__FirstPageTurnLightOn methods */
void   nspanel_mqttmanager_command__first_page_turn_light_on__init
                     (NSPanelMQTTManagerCommand__FirstPageTurnLightOn         *message);
/* NSPanelMQTTManagerCommand__FirstPageTurnLightOff methods */
void   nspanel_mqttmanager_command__first_page_turn_light_off__init
                     (NSPanelMQTTManagerCommand__FirstPageTurnLightOff         *message);
/* NSPanelMQTTManagerCommand__LightCommand methods */
void   nspanel_mqttmanager_command__light_command__init
                     (NSPanelMQTTManagerCommand__LightCommand         *message);
/* NSPanelMQTTManagerCommand methods */
void   nspanel_mqttmanager_command__init
                     (NSPanelMQTTManagerCommand         *message);
size_t nspanel_mqttmanager_command__get_packed_size
                     (const NSPanelMQTTManagerCommand   *message);
size_t nspanel_mqttmanager_command__pack
                     (const NSPanelMQTTManagerCommand   *message,
                      uint8_t             *out);
size_t nspanel_mqttmanager_command__pack_to_buffer
                     (const NSPanelMQTTManagerCommand   *message,
                      ProtobufCBuffer     *buffer);
NSPanelMQTTManagerCommand *
       nspanel_mqttmanager_command__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   nspanel_mqttmanager_command__free_unpacked
                     (NSPanelMQTTManagerCommand *message,
                      ProtobufCAllocator *allocator);
/* --- per-message closures --- */

typedef void (*NSPanelWarning_Closure)
                 (const NSPanelWarning *message,
                  void *closure_data);
typedef void (*NSPanelStatusReport_Closure)
                 (const NSPanelStatusReport *message,
                  void *closure_data);
typedef void (*NSPanelLightStatus_Closure)
                 (const NSPanelLightStatus *message,
                  void *closure_data);
typedef void (*NSPanelRoomStatus_Closure)
                 (const NSPanelRoomStatus *message,
                  void *closure_data);
typedef void (*NSPanelWeatherUpdate__ForecastItem_Closure)
                 (const NSPanelWeatherUpdate__ForecastItem *message,
                  void *closure_data);
typedef void (*NSPanelWeatherUpdate_Closure)
                 (const NSPanelWeatherUpdate *message,
                  void *closure_data);
typedef void (*NSPanelMQTTManagerCommand__FirstPageTurnLightOn_Closure)
                 (const NSPanelMQTTManagerCommand__FirstPageTurnLightOn *message,
                  void *closure_data);
typedef void (*NSPanelMQTTManagerCommand__FirstPageTurnLightOff_Closure)
                 (const NSPanelMQTTManagerCommand__FirstPageTurnLightOff *message,
                  void *closure_data);
typedef void (*NSPanelMQTTManagerCommand__LightCommand_Closure)
                 (const NSPanelMQTTManagerCommand__LightCommand *message,
                  void *closure_data);
typedef void (*NSPanelMQTTManagerCommand_Closure)
                 (const NSPanelMQTTManagerCommand *message,
                  void *closure_data);

/* --- services --- */


/* --- descriptors --- */

extern const ProtobufCEnumDescriptor    nspanel_warning_level__descriptor;
extern const ProtobufCMessageDescriptor nspanel_warning__descriptor;
extern const ProtobufCMessageDescriptor nspanel_status_report__descriptor;
extern const ProtobufCEnumDescriptor    nspanel_status_report__state__descriptor;
extern const ProtobufCMessageDescriptor nspanel_light_status__descriptor;
extern const ProtobufCMessageDescriptor nspanel_room_status__descriptor;
extern const ProtobufCMessageDescriptor nspanel_weather_update__descriptor;
extern const ProtobufCMessageDescriptor nspanel_weather_update__forecast_item__descriptor;
extern const ProtobufCMessageDescriptor nspanel_mqttmanager_command__descriptor;
extern const ProtobufCMessageDescriptor nspanel_mqttmanager_command__first_page_turn_light_on__descriptor;
extern const ProtobufCMessageDescriptor nspanel_mqttmanager_command__first_page_turn_light_off__descriptor;
extern const ProtobufCMessageDescriptor nspanel_mqttmanager_command__light_command__descriptor;
extern const ProtobufCEnumDescriptor    nspanel_mqttmanager_command__affect_lights_options__descriptor;

PROTOBUF_C__END_DECLS


#endif  /* PROTOBUF_C_protobuf_5fnspanel_2eproto__INCLUDED */
