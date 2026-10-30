#pragma once
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stddef.h>
#include <stdint.h>
#include <string>

/**
 * @brief Flow control for writes to the Nextion display, shared by every writer.
 *
 * The display has a single serial input buffer of about 1 KB. When it fills, the display drops new
 * commands and reports 0x24 once it catches up. The ESP cannot see how full that buffer is, so this
 * class counts instead. The display runs at bkcmd=3 and answers every command in order, so the bytes
 * sent and not yet answered are the bytes it has not processed yet. A writer waits, for a bounded
 * time, while more than the window's worth of bytes are unanswered.
 *
 * The count can drift. Some commands are never answered (printh), some answer with something other
 * than 0x01 (get answers with its value), and a raw command from MQTT can be anything. So every
 * MARKER_INTERVAL bytes a "printh" marker is sent, which the display echoes once it has processed
 * everything before it. An entry still ahead of an echoed marker will never be answered and is
 * dropped, so the count is exact again from each marker on. A marker is also sent when replies stop
 * coming and after a 0x24. If the display restarts (bkcmd falls back to 2), or a marker is never
 * echoed, accounting starts over.
 */
class NextionWindow {
public:
  /**
   * @brief Create the monitor task. Call once, before start_locked().
   */
  static void init();

  /**
   * @brief Switch the display to bkcmd=3 and start accounting. The caller holds Nextion::_uart_write_mutex.
   * The window size and timeouts are worked out here from Nextion::get_link_baud(), so call this again
   * after the link baud rate changes.
   */
  static void start_locked();

  /**
   * @brief Stop accounting, for a TFT update. The caller holds Nextion::_uart_write_mutex.
   */
  static void stop_locked();

  /**
   * @brief Wait until the display has room for another command, or the maximum wait has passed.
   * Call without holding Nextion::_uart_write_mutex, so other writers are not held up meanwhile.
   */
  static void wait_for_room();

  /**
   * @brief Write one command and its terminator, accounting for it. The caller holds Nextion::_uart_write_mutex.
   */
  static void write_locked(const std::string &command);

  /**
   * @brief Inspect one frame from the display, without its terminator. Runs on the UART event task.
   * @return true if the frame was a reply to a command and must not be passed on.
   */
  static bool on_frame(const uint8_t *data, size_t size);

private:
  enum class entry_kind : uint8_t {
    COMMAND,
    MARKER,
  };

  struct entry_t {
    int64_t sent_us;
    uint16_t size;
    entry_kind kind;
    uint8_t seq;
    // Waiting when the display reported an overflow. It sends 0x24 only once it resumes, so everything
    // sent during the stall is still waiting at that moment, and some of it was dropped.
    bool overflowed;
    char prefix[16];
  };

  struct stats_t {
    uint32_t commands;
    uint32_t errors;
    uint32_t missing;
    uint32_t lost_in_overflow;
    uint32_t unsolicited;
    uint32_t resets;
    uint32_t waits;
    uint32_t wait_timeouts;
    uint32_t wait_max_ms;
  };

  static void _task_monitor(void *param);
  static void _reset_locked();
  static void _push(entry_kind kind, uint16_t size, const char *prefix, uint8_t seq);
  static void _send_marker_locked(bool probe);
  static void _on_reply(uint8_t code, int64_t now);
  static void _on_marker_echo(uint8_t seq, int64_t now);
  static void _log_stats(bool anomaly);
  static void _configure_for_baud(uint32_t baud);
  static void _post_display_restarted();
  static uint32_t _wire_ms(uint32_t bytes, uint32_t baud);

  // These were measured on Office at 115200 baud. _configure_for_baud() scales them to the link's rate.
  static constexpr uint32_t MEASURED_BAUD = 115200;
  // About a third of the display's ~1.08 KB buffer. On Office, 384 B gave the shortest wait for a tap
  // during an album art render at almost the same throughput as 768 B. A tap waits for at most about
  // one window to drain, so the window is really a time (384 B is ~33 ms on the wire at 115200) and
  // scales with the baud rate...
  static constexpr uint32_t MEASURED_WINDOW_BYTES = 384;
  // ...but not past what the display can hold: a 1 KB window never overflowed it and a 2 KB one did.
  static constexpr uint32_t MAX_WINDOW_BYTES = 768;
  // At low rates the wire time dominates anyway; don't shrink the window to less than a few commands.
  static constexpr uint32_t MIN_WINDOW_BYTES = 128;
  // A page change takes ~400 ms to redraw before the display answers again. This is the display's own
  // time, not the wire's, and does not change with the baud rate.
  static constexpr uint32_t REDRAW_MS = 400;
  // The measured timeouts, kept as floors. A slower link adds the wire time of a full window.
  static constexpr uint32_t MEASURED_MAX_WAIT_MS = 1000;
  static constexpr uint32_t MEASURED_STALL_MS = 500;
  static constexpr uint32_t MEASURED_PROBE_TIMEOUT_MS = 3000;
  // A marker's cost is a share of the bytes sent, so its interval stays in bytes at any rate.
  static constexpr uint32_t MARKER_INTERVAL = 1024;
  static constexpr uint32_t MAX_FAILED_STARTS = 3;
  static constexpr uint8_t MARKER_HEAD = 0xA5;
  // One entry per unanswered command. The shortest commands are about 8 bytes, so a full window of
  // them, plus the one command let past it, needs this many before the oldest has to be dropped.
  static constexpr size_t FIFO_SIZE = 128;
  static_assert(FIFO_SIZE * 8 >= MAX_WINDOW_BYTES + 128, "FIFO_SIZE cannot hold a full window of short commands");

  static inline portMUX_TYPE _lock = portMUX_INITIALIZER_UNLOCKED;
  // Given whenever bytes are answered, to wake a writer waiting for room.
  static inline SemaphoreHandle_t _room_sem = NULL;

  // Only changed with Nextion::_uart_write_mutex held.
  static inline volatile bool _accounting = false;
  static inline uint32_t _bytes_since_marker = 0;
  static inline uint8_t _next_seq = 1;

  // Under _lock.
  static inline entry_t _fifo[FIFO_SIZE];
  static inline size_t _fifo_head = 0;
  static inline size_t _fifo_count = 0;
  static inline uint32_t _inflight_bytes = 0;
  static inline int64_t _last_reply_us = 0;
  static inline int16_t _probe_seq = -1;
  static inline int64_t _probe_sent_us = 0;
  // From start until the first marker is echoed: bkcmd=3 itself may or may not be answered, depending on the level it replaces.
  static inline bool _settling = false;
  static inline bool _resync_requested = false;
  static inline bool _restart_requested = false;
  static inline stats_t _stats = {};

  // Set by _configure_for_baud() when the window starts; read from every writer and the monitor task.
  static inline std::atomic<uint32_t> _window_bytes = MEASURED_WINDOW_BYTES;
  static inline std::atomic<uint32_t> _max_wait_ms = MEASURED_MAX_WAIT_MS;
  static inline std::atomic<uint32_t> _stall_ms = MEASURED_STALL_MS;
  static inline std::atomic<uint32_t> _probe_timeout_ms = MEASURED_PROBE_TIMEOUT_MS;
};
