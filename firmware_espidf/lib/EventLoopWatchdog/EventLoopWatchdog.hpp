#pragma once
#include <atomic>
#include <esp_event.h>
#include <stdint.h>
#include <string>
#include <vector>

ESP_EVENT_DECLARE_BASE(EVENTLOOPWATCHDOG_EVENT);

/**
 * @brief Diagnostic: notices when the default event loop or the MQTT client stops, and logs every
 *        task's backtrace so the handler it is stuck in, and what that handler waits on, can be found.
 *
 * Default event loop: a probe event is posted every few seconds. If none has been handled for
 * loop_stall_after_ms, every task's backtrace is logged, a line per task, through the normal log
 * path, so it reaches the manager over MQTT. That happens once per stall: it is armed again only
 * after the loop has dispatched a probe.
 *
 * MQTT: esp-mqtt runs its event handlers on its task holding its API lock, which every publish and
 * subscribe takes too. A handler that never returns stops all MQTT for good: the panel no longer
 * reacts to commands or sends anything. A probe task takes the API lock every few seconds. If it has
 * waited for mqtt_stall_after_ms, the backtraces can not go out over MQTT, so they are printed to the
 * serial port and stored in mqtt_stall_path, and the panel restarts. Once MQTT is connected after the
 * restart, the stored backtraces are logged, so they reach the manager, and removed.
 *
 * Decode a line with:
 *   xtensa-esp32-elf-addr2line -pfiaC -e .pio/build/original_sonoff/firmware.elf <pc> <pc> ...
 */
class EventLoopWatchdog {
public:
  /**
   * @brief Register the probe handler and start the watchdog tasks. Call after the default loop exists.
   */
  static void init();

private:
  static void _probe_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
  static void _task_watch(void *param);
  static void _task_probe_mqtt(void *param);

  /**
   * Every task's backtrace, a line per task: "<task name> Backtrace: pc:sp pc:sp ...".
   */
  static std::vector<std::string> _capture_all_backtraces();
  static void _log_all_backtraces();
  static void _capture_putc(char c);

  /**
   * MQTT has been stuck for mqtt_stall_after_ms: store the backtraces and restart.
   */
  [[noreturn]] static void _handle_mqtt_stall(int64_t stuck_ms);

  /**
   * Log the backtraces stored by _handle_mqtt_stall() before the last restart, if any, and remove them.
   */
  static void _report_stored_mqtt_stall();

  static constexpr uint32_t probe_interval_ms = 2000;
  static constexpr uint32_t loop_stall_after_ms = 15000;
  static constexpr uint32_t mqtt_probe_interval_ms = 5000;
  // esp-mqtt also holds its lock while it connects, which can take a network timeout or two.
  static constexpr uint32_t mqtt_stall_after_ms = 60000;
  static constexpr const char *mqtt_stall_path = "/littlefs/mqtt_stall.txt";
  static constexpr int backtrace_depth = 16;
  static constexpr size_t capture_size = 16 * 1024;

  static inline std::atomic<int64_t> _last_handled_us = 0;
  // When the MQTT probe started waiting for the API lock, 0 while it is not waiting.
  static inline std::atomic<int64_t> _mqtt_probe_started_us = 0;

  // Written only by _capture_putc() while the scheduler is suspended, read after.
  static inline char *_capture = nullptr;
  static inline size_t _capture_len = 0;
};
