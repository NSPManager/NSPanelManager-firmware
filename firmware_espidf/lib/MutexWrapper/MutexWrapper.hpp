#pragma once
#include <esp_event.h> // Included for base definitions like portMAX_DELAY and such
#include <freertos/semphr.h>

template <typename T>
class MutexWrapped {
public:
  MutexWrapped() : _value{} {
    this->_mutex = portMUX_INITIALIZER_UNLOCKED;
  }

  MutexWrapped(T value) {
    this->_mutex = portMUX_INITIALIZER_UNLOCKED;
    this->_value = value;
  }

  T get() {
    portENTER_CRITICAL(&this->_mutex);
    T current_value = this->_value;
    portEXIT_CRITICAL(&this->_mutex);
    return current_value;
  }

  void set(T new_value) {
    portENTER_CRITICAL(&this->_mutex);
    this->_value = new_value;
    portEXIT_CRITICAL(&this->_mutex);
  }

private:
  T _value;
  portMUX_TYPE _mutex;
};