#pragma once
#include <esp_private/system_internal.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * @brief Restart the panel, even if the default event loop is stuck.
 *
 * esp_restart() runs the shutdown handlers before it resets the chip. One of them is
 * esp_wifi_stop(), and the Wi-Fi driver posts its stop events to the default event loop with
 * portMAX_DELAY. If that loop's queue is full because a handler never returns, esp_wifi_stop() waits
 * forever and the panel never restarts. It sits on whatever page was last drawn ("Rebooting", for a
 * reboot command) until it is power cycled.
 *
 * A panel is most likely to need a restart exactly when something is stuck, so a high priority task
 * resets the chip without the shutdown handlers if esp_restart() hasn't finished in time. In the
 * normal case esp_restart() finishes first and Wi-Fi is still stopped cleanly.
 */
namespace PanelRestart {

// esp_restart() takes well under a second when nothing is stuck.
static constexpr uint32_t backstop_ms = 5000;

inline void _task_backstop(void *param) {
  vTaskDelay(pdMS_TO_TICKS(PanelRestart::backstop_ms));
  // Nothing is logged here: the log forwarder may be one of the things that is stuck.
  esp_restart_noos();
}

[[noreturn]] inline void restart() {
  // Highest priority so that nothing it is waiting behind can starve it. It only ever sleeps.
  if (xTaskCreate(&PanelRestart::_task_backstop, "restart_backstop", 2048, NULL, configMAX_PRIORITIES - 1, NULL) != pdPASS) [[unlikely]] {
    // Without the backstop, a hang in the shutdown handlers would be for good. Skip them instead.
    esp_restart_noos();
  }
  esp_restart();
}

} // namespace PanelRestart
