#pragma once
// ESP-IDF port of ptaru/nspanel-terminal's NSPanelDisplay (protocol 8 UART
// framebuffer streamer). ESPHome pieces removed:
//   display::Display base class, Component lifecycle, uart::IDFUARTComponent,
//   RAMAllocator, HighFrequencyLoopRequester, App.feed_wdt(), NSPanelTouchscreen.
// Replaced by:
//   plain uart_port_t + thin UART helpers, heap_caps_*, a self-owned control
//   task, LVGL 9 flush callback + lv_indev touch input.
//
// Threading rule (avoids deadlocks):
//   lock order is always  LVGL lock  ->  mutex_.
//   Nothing that holds mutex_ ever calls into LVGL. Repaints requested from
//   the transport/control tasks are applied by an lv_timer that runs inside
//   the LVGL task.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "frame_slot.h"
#include "stream_engine.h"

namespace nspanel_terminal {

// ---- Arduino/ESPHome-style time helpers so the .cpp body barely changes ----
inline uint32_t micros() { return static_cast<uint32_t>(esp_timer_get_time()); }
inline uint32_t millis() { return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL); }
inline void delay(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
inline void feed_wdt() { esp_task_wdt_reset(); } // harmless if task isn't subscribed

class NSPanelDisplay {
public:
  struct Config {
    // UART already installed (uart_driver_install with a NON-ZERO tx buffer)
    // and pinned to the Nextion by your existing firmware.
    uart_port_t uart_port = UART_NUM_1;
    uint16_t width = 454; // EU 454x320, US 320x480 (check your TFT)
    uint16_t height = 320;
    // false: 4 encoded frame slots in internal RAM (falls back to PSRAM).
    // true : allocate them in PSRAM (requires PSRAM enabled in sdkconfig).
    bool frame_slots_psram = false;
    int initial_brightness = -1; // 0-100, or -1 to leave untouched
    std::string tft_url;         // optional: HTTP URL for TFT upload (whmi-wris)
  };

  // Single, process-lifetime instance (constructed on first use, never goes
  // out of scope, so LVGL/task callbacks can never dangle).
  static NSPanelDisplay &instance();

  NSPanelDisplay(const NSPanelDisplay &) = delete;
  NSPanelDisplay &operator=(const NSPanelDisplay &) = delete;
  ~NSPanelDisplay();

  // Replaces setup(): starts the transport task and the control task
  // (the control task runs what used to be loop()).
  esp_err_t begin(const Config &cfg);
  // Replaces on_shutdown(): asks the panel to leave stream mode, stops tasks.
  void end();

  // Call once, WITH THE LVGL LOCK HELD, after lv_display_create() and
  // lv_display_set_buffers() (RGB565, partial render mode, no byte swap).
  // Installs the flush callback, a touch indev, refresh-gating events and the
  // repaint timer. Returns false on failure.
  bool attach_lvgl(lv_display_t *display);
  lv_indev_t *indev() const { return lvgl_indev_; }

  // LVGL flush path (replaces display::Display::draw_pixels_at).
  // px must be RGB565 little-endian (LV_COLOR_FORMAT_RGB565, no swap).
  // stride_bytes: bytes per source row (w * 2 in partial render mode).
  // Encodes synchronously, so LVGL may reuse the buffer when this returns.
  void draw_pixels_at(int x, int y, int w, int h, const uint8_t *px, size_t stride_bytes);

  void request_exit() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    exit_requested_ = true;
    stream_.request_exit();
    wake_transport_();
  }
  void request_enter();
  void request_tft_upload();
  void request_brightness(int percent);
  // Safe from any task: the actual lv_obj_invalidate happens in the LVGL task.
  void request_full_redraw() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    request_repaint_();
  }
  void set_refresh_active(bool active) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    refresh_active_ = active;
  }
  uint16_t width() const { return width_; }
  uint16_t height() const { return height_; }
  std::recursive_mutex &transport_mutex() { return mutex_; }

private:
  NSPanelDisplay() = default;

  enum class State {
    BOOT_WAIT,
    CONNECT,
    PAGE,
    BKCMD,
    SYS0,
    SYS1,
    SYS2,
    CONFIG,
    SWITCHING,
    STREAM,
    RECOVER_WAIT,
    RECOVER_CONFIG,
    RECOVER_BAUD,
    VERIFY_SYS0,
    VERIFY_SYS1,
    VERIFY_SYS2,
    VERIFY_BKCMD,
    VERIFY_PAGE,
    VERIFY_IDENTITY,
    BRIGHTNESS_SETTLE,
    BRIGHTNESS_VERIFY,
    STOPPED
  };

  // ---- Frame jobs: flush areas are PackBits-encoded into these slots ----
  static constexpr unsigned JOB_ROWS = 16;
  static constexpr unsigned JOB_SLOTS = 4;
  struct FrameJob : FrameSlot {
    uint8_t *cache = nullptr;
    size_t size = 0;
    uint16_t x = 0, y = 0, w = 0, h = 0;
  };
  std::array<FrameJob, JOB_SLOTS> jobs_{};
  uint8_t *job_storage_ = nullptr;
  size_t job_capacity_ = 0;
  unsigned job_queue_[JOB_SLOTS]{};
  unsigned job_head_ = 0, job_tail_ = 0, job_queued_ = 0;
  FrameJob *active_job_ = nullptr;
  uint64_t job_generation_ = 0;
  bool repaint_pending_ = true;
  bool unsupported_logged_ = false;
  bool frame_slots_internal_ = false;
  bool frame_slots_psram_ = false;

  // heap_caps replacements for RAMAllocator (PREFER_INTERNAL / ALLOC_EXTERNAL)
  uint8_t *alloc_frame_slots_(size_t bytes);
  void free_frame_slots_();

  // ---- UART helpers (replace uart::UARTDevice + IDFUARTComponent) ----
  uart_port_t port_ = UART_NUM_1;
  size_t available() const {
    size_t n = 0;
    uart_get_buffered_data_len(port_, &n);
    return n;
  }
  size_t available_for_write() const {
    size_t n = 0;
    uart_get_tx_buffer_free_size(port_, &n); // needs a non-zero TX ring buffer
    return n;
  }
  bool read_byte(uint8_t *b) { return uart_read_bytes(port_, b, 1, 0) == 1; }
  bool read_array(uint8_t *buf, size_t n) {
    return uart_read_bytes(port_, buf, static_cast<uint32_t>(n), 0) == static_cast<int>(n);
  }
  void write_array(const uint8_t *buf, size_t n) { uart_write_bytes(port_, buf, n); }
  void write_byte(uint8_t b) { uart_write_bytes(port_, &b, 1); }
  void write_str(const char *s);

  void send_command_(const char *);
  void send_query_(State, const char *);
  bool pump_command_();
  bool read_fixed_(size_t);
  bool read_terminated_();
  void drain_();
  bool change_baud_(uint32_t);
  bool identity_ok_(const char *reply) const;
  void fail_(const char *);
  void finish_recovery_();

  void wake_transport_() {
    if (work_signal_ != nullptr)
      xSemaphoreGive(work_signal_);
  }
  void signal_slot_() {
    if (slot_signal_ != nullptr)
      xSemaphoreGive(slot_signal_);
  }
  StaticSemaphore_t work_signal_storage_{};
  SemaphoreHandle_t work_signal_ = nullptr;
  StaticSemaphore_t slot_signal_storage_{};
  SemaphoreHandle_t slot_signal_ = nullptr;

  void service_stream_(int incoming = -1);
  bool init_frame_jobs_();
  FrameJob *acquire_job_(std::unique_lock<std::recursive_mutex> &lock);
  void enqueue_job_(FrameJob *job);
  FrameJob *dequeue_job_();
  void release_active_job_();
  void clear_jobs_();
  bool job_queue_empty_() const { return job_queued_ == 0; }
  void request_repaint_(); // sets invalidate_requested_ only; never calls LVGL
  void drop_frame_(const char *reason);

  // ---- TFT upload (unchanged; already esp_http_client based) ----
  bool upload_tft_();
  bool upload_ack_(uint32_t &position, uint32_t expected);
  bool start_upload_task_();
  static void upload_task_entry_(void *arg);

  // ---- State machine ----
  State state_ = State::BOOT_WAIT;
  uint32_t deadline_ = 0, next_command_byte_ = 0;
  uint8_t config_step_ = 0, recovery_step_ = 0, page_ = 0, saved_bkcmd_ = 3;
  uint32_t saved_sys_[3]{};
  uint16_t session_ = 0;
  bool snapshots_ready_ = false, exit_requested_ = false, terminal_active_ = false, restore_page_ = false;
  uint8_t startup_attempts_ = 0;
  bool startup_retry_ = true;
  std::array<uint8_t, 128> rx_{};
  size_t rx_count_ = 0;
  std::vector<uint8_t> command_;
  size_t command_at_ = 0;
  uint16_t width_ = 454, height_ = 320;
  StreamEngine stream_;
  // (DirtyRows dirty_ removed: there is no pixel mirror any more, LVGL owns
  //  dirty tracking. Delete the dirty_.* lines in the .cpp.)

  // Replaces HighFrequencyLoopRequester: while true, the control task spins
  // (taskYIELD) instead of sleeping a tick, so the 190 us byte pacing in
  // pump_command_() holds.
  std::atomic<bool> fast_loop_{false};

  // ---- Touch: transport task pushes, LVGL indev read callback pops ----
  TouchQueue pending_touches_;
  bool touch_reset_pending_ = false;
  bool touch_down_ = false;
  bool have_lookahead_ = false;
  TouchSample lookahead_{};
  int16_t touch_x_ = 0, touch_y_ = 0;
  unsigned dropped_touches_ = 0, dropped_frames_ = 0;

  bool upload_pending_ = false;
  int16_t initial_brightness_ = -1;
  uint8_t brightness_percent_ = 100;
  bool brightness_selected_ = false;
  bool brightness_pending_ = false, brightness_resume_ = false;
  std::string tft_url_;

  // ---- LVGL 9 glue (all callbacks run in the LVGL task, LVGL lock held) ----
  lv_display_t *lvgl_display_ = nullptr;
  lv_indev_t *lvgl_indev_ = nullptr;
  lv_timer_t *lvgl_timer_ = nullptr;
  std::atomic<bool> invalidate_requested_{false};
  static void lvgl_flush_cb_(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
  static void lvgl_read_cb_(lv_indev_t *indev, lv_indev_data_t *data);
  static void lvgl_timer_cb_(lv_timer_t *timer);
  static void lvgl_refr_start_cb_(lv_event_t *e);
  static void lvgl_refr_ready_cb_(lv_event_t *e);
  void on_lvgl_refresh_start_();
  void on_lvgl_refresh_ready_();
  void read_touch_(lv_indev_data_t *data);

  // ---- Tasks ----
  static constexpr uint32_t TRANSPORT_STACK = 6144;
  static constexpr uint32_t CONTROL_STACK = 4096;
  static constexpr UBaseType_t TRANSPORT_PRIO = 2;
  static constexpr UBaseType_t CONTROL_PRIO = 1;
  static constexpr BaseType_t TASK_CORE = 0;
  static void task_entry_(void *arg); // transport (UART streaming)
  void task_loop_();
  static void control_entry_(void *arg); // runs loop() every tick
  void loop();                           // state machine (was Component::loop)
  void dump_config();

  mutable std::recursive_mutex mutex_;
  std::atomic<bool> task_stop_{false};
  std::atomic<bool> task_running_{false};
  std::atomic<bool> control_stop_{false};
  std::atomic<bool> control_running_{false};
  std::atomic<bool> upload_running_{false};
  std::atomic<bool> upload_finished_{false};
  std::atomic<bool> upload_ok_{false};
  TaskHandle_t task_ = nullptr;
  TaskHandle_t control_task_ = nullptr;
  bool refresh_active_ = false;
  bool lvgl_refreshing_ = false;
};

} // namespace nspanel_terminal