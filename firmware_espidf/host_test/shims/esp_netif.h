#pragma once
#include "esp_err.h"
#include <stdint.h>
typedef struct esp_netif_obj esp_netif_t;
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { esp_ip4_addr_t ip; esp_ip4_addr_t netmask; esp_ip4_addr_t gw; } esp_netif_ip_info_t;

#define IP4ADDR_STRLEN_MAX 16
#define IPSTR "%d.%d.%d.%d"
#define esp_ip4_addr1_16(ipaddr) ((uint8_t)((ipaddr)->addr >> 0) & 0xff)
#define esp_ip4_addr2_16(ipaddr) ((uint8_t)((ipaddr)->addr >> 8) & 0xff)
#define esp_ip4_addr3_16(ipaddr) ((uint8_t)((ipaddr)->addr >> 16) & 0xff)
#define esp_ip4_addr4_16(ipaddr) ((uint8_t)((ipaddr)->addr >> 24) & 0xff)
#define IP2STR(ipaddr) esp_ip4_addr1_16(ipaddr), esp_ip4_addr2_16(ipaddr), esp_ip4_addr3_16(ipaddr), esp_ip4_addr4_16(ipaddr)
esp_err_t esp_netif_init(void);
