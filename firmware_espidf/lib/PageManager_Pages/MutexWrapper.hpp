#pragma once
#include <esp_event.h> // Included for base definitions like portMAX_DELAY and such
#include <freertos/semphr.h>

template <typename T>
class MutexWrapped {
public:
  MutexWrapped() {
    this->_mutex = xSemaphoreCreateMutex();
  }

  T get() {
    if (xSemaphoreTake(this->_mutex, portMAX_DELAY) == pdPASS) {
      T current_value = this->_value;
      xSemaphoreGive(this->_mutex);
      return current_value;
    }
    return NULL;
  }

  void set(T new_value) {
    if (xSemaphoreTake(this->_mutex, portMAX_DELAY) == pdPASS) {
      this->_value = new_value;
      xSemaphoreGive(this->_mutex);
    }
  }

private:
  T _value;
  SemaphoreHandle_t _mutex;
};