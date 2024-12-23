#pragma once
#include <esp_event.h>

ESP_EVENT_DECLARE_BASE(STATUSUPDATEMANAGER_EVENT);

enum statusupdatemanagerevent_t {
  AVERAGE_TEMP_UPDATE,
};