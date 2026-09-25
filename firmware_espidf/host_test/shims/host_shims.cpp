// Host implementations of the ESP-IDF primitives the firmware touches.
// Events dispatch synchronously on the posting thread so tests are deterministic.
#include "arch_rules.hpp"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

const char *esp_err_to_name(esp_err_t code) { return "HOST_ERR"; }
void esp_log_level_set(const char *, esp_log_level_t) {}

// ---------- critical sections ----------
static std::recursive_mutex g_critical;
void portENTER_CRITICAL(portMUX_TYPE *) { g_critical.lock(); }
void portEXIT_CRITICAL(portMUX_TYPE *) { g_critical.unlock(); }

// ---------- semaphores ----------
namespace {
struct HostSemaphore {
  std::timed_mutex mtx;
  bool is_mutex;
  TaskHandle_t holder = nullptr;
};
} // namespace

SemaphoreHandle_t xSemaphoreCreateMutex(void) { auto *s = new HostSemaphore(); s->is_mutex = true; return s; }
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) { return xSemaphoreCreateMutex(); }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { auto *s = new HostSemaphore(); s->is_mutex = false; s->mtx.lock(); return s; }
void vSemaphoreDelete(SemaphoreHandle_t sem) { delete static_cast<HostSemaphore *>(sem); }

BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks) {
  if (sem == nullptr) return pdFALSE;
  arch::check_blocking_call("xSemaphoreTake", ticks);
  auto *s = static_cast<HostSemaphore *>(sem);
  bool got = (ticks == portMAX_DELAY) ? (s->mtx.lock(), true)
                                      : s->mtx.try_lock_for(std::chrono::milliseconds(ticks));
  if (got) s->holder = xTaskGetCurrentTaskHandle();
  return got ? pdTRUE : pdFALSE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t sem) {
  if (sem == nullptr) return pdFALSE;
  auto *s = static_cast<HostSemaphore *>(sem);
  s->holder = nullptr;
  s->mtx.unlock();
  return pdTRUE;
}

TaskHandle_t xSemaphoreGetMutexHolder(SemaphoreHandle_t sem) {
  return sem ? static_cast<HostSemaphore *>(sem)->holder : nullptr;
}

// ---------- tasks ----------
void vTaskDelay(TickType_t ticks) {
  arch::check_blocking_call("vTaskDelay", ticks);
  std::this_thread::sleep_for(std::chrono::milliseconds(ticks));
}
void vTaskDelete(TaskHandle_t) {}
TaskHandle_t xTaskGetCurrentTaskHandle(void) {
  static thread_local int marker = 0;
  return &marker;
}
BaseType_t xTaskCreate(TaskFunction_t fn, const char *, uint32_t, void *arg, UBaseType_t, TaskHandle_t *out) {
  // Tests drive tasks explicitly; creation is recorded, not executed.
  if (out) *out = nullptr;
  return pdPASS;
}
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *n, uint32_t s, void *a, UBaseType_t p, TaskHandle_t *out, BaseType_t) {
  return xTaskCreate(fn, n, s, a, p, out);
}
BaseType_t xTaskNotifyGive(TaskHandle_t) { return pdPASS; }
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t ticks) {
  arch::check_blocking_call("ulTaskNotifyTake", ticks);
  return 0;
}

// ---------- event loop ----------
namespace {
struct Handler {
  esp_event_base_t base;
  int32_t id;
  esp_event_handler_t fn;
  void *arg;
};
struct Pending {
  esp_event_base_t base;
  int32_t id;
  std::vector<uint8_t> data;
  bool has_data;
};
struct Loop {
  std::string name;
  std::vector<Handler> handlers;
  std::deque<Pending> pending;
  bool dispatching = false;
};
Loop &default_loop() {
  static Loop l{"sys_evt", {}, {}, false};
  return l;
}
Loop *as_loop(esp_event_loop_handle_t h) { return h ? static_cast<Loop *>(h) : &default_loop(); }

// arch_rules identifies the default loop as nullptr, since that is what the
// firmware passes when it calls esp_event_post() rather than esp_event_post_to().
const void *loop_id(Loop *l) { return l == &default_loop() ? nullptr : l; }

bool matches(const Handler &h, esp_event_base_t base, int32_t id) {
  if (h.base != nullptr && std::strcmp(h.base, base) != 0) return false;
  return h.id == ESP_EVENT_ANY_ID || h.id == id;
}

esp_err_t do_register(Loop *l, esp_event_base_t base, int32_t id, esp_event_handler_t fn, void *arg) {
  l->handlers.push_back({base, id, fn, arg});
  return ESP_OK;
}
esp_err_t do_unregister(Loop *l, esp_event_base_t base, int32_t id, esp_event_handler_t fn) {
  for (auto it = l->handlers.begin(); it != l->handlers.end(); ++it) {
    if (it->fn == fn && it->id == id) { l->handlers.erase(it); return ESP_OK; }
  }
  return ESP_ERR_NOT_FOUND;
}
void deliver(Loop *l, esp_event_base_t base, int32_t id, const void *data) {
  auto snapshot = l->handlers; // handlers may unregister during dispatch
  for (auto &h : snapshot) {
    if (!matches(h, base, id)) continue;
    arch::enter_dispatch(loop_id(l), l->name.c_str(), base, id, reinterpret_cast<const void *>(h.fn));
    h.fn(h.arg, base, id, const_cast<void *>(data));
    arch::exit_dispatch();
  }
}

esp_err_t do_post(Loop *l, esp_event_base_t base, int32_t id, const void *data, size_t size) {
  // A post to the loop currently being dispatched is queued, not run inline:
  // on the device the loop task finishes the current handler before it calls
  // xQueueReceive() again. Deferred payloads are copied, as esp_event does,
  // because the posting handler's frame is gone by the time they are delivered.
  if (l->dispatching) {
    Pending p{base, id, {}, data != nullptr && size != 0};
    if (p.has_data) {
      p.data.assign(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
    }
    l->pending.push_back(std::move(p));
    return ESP_OK;
  }

  l->dispatching = true;
  deliver(l, base, id, data);
  while (!l->pending.empty()) {
    Pending p = std::move(l->pending.front());
    l->pending.pop_front();
    deliver(l, p.base, p.id, p.has_data ? p.data.data() : nullptr);
  }
  l->dispatching = false;
  return ESP_OK;
}
} // namespace

esp_err_t esp_event_loop_create_default(void) { return ESP_OK; }
esp_err_t esp_event_loop_create(const esp_event_loop_args_t *args, esp_event_loop_handle_t *out) {
  auto *l = new Loop();
  l->name = (args != nullptr && args->task_name != nullptr) ? args->task_name : "unnamed";
  *out = l;
  return ESP_OK;
}
esp_err_t esp_event_loop_delete(esp_event_loop_handle_t loop) { delete static_cast<Loop *>(loop); return ESP_OK; }
esp_err_t esp_event_handler_register(esp_event_base_t b, int32_t i, esp_event_handler_t f, void *a) { return do_register(&default_loop(), b, i, f, a); }
esp_err_t esp_event_handler_unregister(esp_event_base_t b, int32_t i, esp_event_handler_t f) { return do_unregister(&default_loop(), b, i, f); }
esp_err_t esp_event_handler_register_with(esp_event_loop_handle_t l, esp_event_base_t b, int32_t i, esp_event_handler_t f, void *a) { return do_register(as_loop(l), b, i, f, a); }
esp_err_t esp_event_handler_unregister_with(esp_event_loop_handle_t l, esp_event_base_t b, int32_t i, esp_event_handler_t f) { return do_unregister(as_loop(l), b, i, f); }
esp_err_t esp_event_post(esp_event_base_t b, int32_t i, const void *d, size_t n, TickType_t ticks) {
  arch::check_post(nullptr, b, i, ticks);
  return do_post(&default_loop(), b, i, d, n);
}
esp_err_t esp_event_post_to(esp_event_loop_handle_t l, esp_event_base_t b, int32_t i, const void *d, size_t n, TickType_t ticks) {
  arch::check_post(loop_id(as_loop(l)), b, i, ticks);
  return do_post(as_loop(l), b, i, d, n);
}
