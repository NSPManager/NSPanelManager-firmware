#pragma once

#include <esp_event.h>

ESP_EVENT_DECLARE_BASE(ROOMMANAGER_EVENT);

enum roommanager_event_t {
  HOME_PAGE_UPDATED,
  ROOM_ENTITIES_PAGE_UPDATED,
};