#pragma once
#include <esp_err.h>

class LittleFS {
public:
  static esp_err_t mount();
  static esp_err_t unmount();
  static bool is_mounted();

private:
  static inline bool _is_mounted;
};