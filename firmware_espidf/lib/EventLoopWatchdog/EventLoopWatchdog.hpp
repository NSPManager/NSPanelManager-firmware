#pragma once
#include <atomic>
#include <esp_event.h>
#include <stdint.h>

ESP_EVENT_DECLARE_BASE(EVENTLOOPWATCHDOG_EVENT);

/**
 * @brief Diagnostic: notices when the default event loop stops dispatching, and logs every task's
 *        backtrace so the handler it is stuck in, and what that handler waits on, can be found.
 *
 * A probe event is posted to the default loop every few seconds. If none has been handled for
 * stall_after_ms, every task's backtrace is captured from esp_rom_printf and logged, a line per
 * task, through the normal log path, so it reaches the manager over MQTT. That happens once per
 * stall: it is armed again only after the loop has dispatched a probe.
 *
 * Decode a line with:
 *   xtensa-esp32-elf-addr2line -pfiaC -e .pio/build/original_sonoff/firmware.elf <pc> <pc> ...
 */
class EventLoopWatchdog {
public:
  /**
   * @brief Register the probe handler and start the watchdog task. Call after the default loop exists.
   */
  static void init();

private:
  static void _probe_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
  static void _task_watch(void *param);
  static void _log_all_backtraces();
  static void _capture_putc(char c);

  static constexpr uint32_t probe_interval_ms = 2000;
  static constexpr uint32_t stall_after_ms = 15000;
  static constexpr int backtrace_depth = 16;
  static constexpr size_t capture_size = 16 * 1024;

  static inline std::atomic<int64_t> _last_handled_us = 0;

  // Written only by _capture_putc() while the scheduler is suspended, read after.
  static inline char *_capture = nullptr;
  static inline size_t _capture_len = 0;
};
