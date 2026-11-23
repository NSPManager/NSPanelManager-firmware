#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <esp_err.h>
#include <esp_netif_ip_addr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * A minimal DNS server for the fallback access point: every A query is answered
 * with the panel's own address, so phones and computers connecting to the AP
 * open the configuration page as a captive portal. Other queries get an empty
 * answer.
 */
class CaptivePortalDns {
public:
  /**
   * Start answering DNS queries on UDP port 53 with this address. Does nothing if already running.
   */
  static esp_err_t start(esp_ip4_addr_t address);

  /**
   * Stop the server. Safe to call when it is not running.
   */
  static void stop();

private:
  static void _task(void *param);

  /**
   * Build the answer to a DNS query in place. Returns the answer's length, or 0 to not answer.
   */
  static size_t _build_response(uint8_t *packet, size_t length, size_t capacity, uint32_t address);

  static inline std::atomic<bool> _running = false;
  static inline TaskHandle_t _task_handle = NULL;
  static inline uint32_t _address = 0; // Network byte order
};
