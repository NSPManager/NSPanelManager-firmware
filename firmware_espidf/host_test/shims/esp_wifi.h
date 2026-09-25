#pragma once
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum { WIFI_AUTH_OPEN = 0, WIFI_AUTH_WEP, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK, WIFI_AUTH_WPA_WPA2_PSK, WIFI_AUTH_MAX } wifi_auth_mode_t;
typedef enum { WIFI_MODE_NULL = 0, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA } wifi_mode_t;
typedef enum { WIFI_IF_STA = 0, WIFI_IF_AP } wifi_interface_t;

typedef struct {
  uint8_t bssid[6];
  uint8_t ssid[33];
  uint8_t primary;
  int8_t rssi;
  wifi_auth_mode_t authmode;
} wifi_ap_record_t;

typedef struct {
  uint8_t ssid[32];
  uint8_t password[64];
  wifi_auth_mode_t authmode;
  uint8_t channel;
  uint8_t max_connection;
} wifi_ap_config_t;

typedef struct {
  uint8_t ssid[32];
  uint8_t password[64];
  wifi_auth_mode_t threshold_authmode;
} wifi_sta_config_t;

typedef union {
  wifi_ap_config_t ap;
  wifi_sta_config_t sta;
} wifi_config_t;

typedef struct { int nvs_enable; int static_rx_buf_num; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){1, 10})

typedef enum { WIFI_EVENT_STA_START, WIFI_EVENT_STA_CONNECTED, WIFI_EVENT_STA_DISCONNECTED, WIFI_EVENT_AP_START } wifi_event_t;

esp_err_t esp_wifi_sta_get_rssi(int *rssi);
esp_err_t esp_wifi_get_mode(wifi_mode_t *mode);
