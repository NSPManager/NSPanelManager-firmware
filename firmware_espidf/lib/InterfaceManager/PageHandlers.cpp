#include <InterfaceManager.hpp>
#include <PageHandlers.hpp>
#include <esp_log.h>

esp_err_t PageHandlers::add(esp_event_base_t event_base, int32_t event_id, esp_event_handler_t event_handler, void *event_handler_arg) {
  return this->add_to(NULL, event_base, event_id, event_handler, event_handler_arg);
}

esp_err_t PageHandlers::add_to(esp_event_loop_handle_t event_loop, esp_event_base_t event_base, int32_t event_id, esp_event_handler_t event_handler, void *event_handler_arg) {
  if (!InterfaceManager::is_dispatched(event_loop, event_base)) [[unlikely]] {
    ESP_LOGE("PageHandlers", "No page dispatcher for event base %s on this loop; add one in InterfaceManager::init().", event_base);
    return ESP_ERR_NOT_SUPPORTED;
  }

  Registration registration = {
      .event_loop = event_loop,
      .event_base = event_base,
      .event_id = event_id,
      .handler = event_handler,
      .arg = event_handler_arg,
  };

  bool retired;
  bool kept = false;
  portENTER_CRITICAL(&this->_lock);
  retired = this->_retired;
  if (!retired && this->_count < MAX_REGISTRATIONS) {
    this->_registrations[this->_count++] = registration;
    kept = true;
  }
  portEXIT_CRITICAL(&this->_lock);

  if (!kept) {
    if (!retired) {
      ESP_LOGE("PageHandlers", "More than %zu handlers registered for one page; raise MAX_REGISTRATIONS.", MAX_REGISTRATIONS);
      return ESP_ERR_NO_MEM;
    }
    // The page was unshown while this show() was still registering.
    return ESP_ERR_INVALID_STATE;
  }
  return ESP_OK;
}

void PageHandlers::retire() {
  portENTER_CRITICAL(&this->_lock);
  this->_retired = true;
  portEXIT_CRITICAL(&this->_lock);
}

void PageHandlers::dispatch(esp_event_loop_handle_t event_loop, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  // Copy the matching handlers out first: they run for a long time (drawing,
  // publishing), and the page may add() more meanwhile.
  std::array<Registration, MAX_REGISTRATIONS> matching;
  size_t count = 0;
  portENTER_CRITICAL(&this->_lock);
  if (!this->_retired) {
    for (size_t i = 0; i < this->_count; i++) {
      const Registration &registration = this->_registrations[i];
      if (registration.event_loop == event_loop && registration.event_base == event_base && (registration.event_id == ESP_EVENT_ANY_ID || registration.event_id == event_id)) {
        matching[count++] = registration;
      }
    }
  }
  portEXIT_CRITICAL(&this->_lock);

  for (size_t i = 0; i < count; i++) {
    matching[i].handler(matching[i].arg, event_base, event_id, event_data);
  }
}
