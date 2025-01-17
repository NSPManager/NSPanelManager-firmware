#pragma once
#include <Nextion_constans.hpp>
#include <esp_event.h>
#include <vector>

ESP_EVENT_DECLARE_BASE(NEXTION_EVENT);

enum nextion_event_t {
  RECEIVED_DATA,
  CONNECTED,
  RECEIVED_NSPM_FLAG,
  TOUCH_EVENT,
  SLEEP_EVENT,
  WAKE_EVENT,
  UPDATE_STARTED,
  UPDATE_READY_FOR_NEXT_CHUNK,
  UPDATE_JUMP_TO_OFFSET,
  UPDATE_FINISHED,
};

// struct nextion_event_data_t {
//   // Numbes of bytes read
//   size_t data_size;

//   // Data buffer
//   uint8_t data[NEXTION_UART_BUFFER_SIZE + 1];
// };

struct nextion_event_touch_t {
  // What page was pressed
  uint8_t page_number;

  // What component was pressed
  uint8_t component_id;

  // Was it a press or release event
  bool pressed;
};