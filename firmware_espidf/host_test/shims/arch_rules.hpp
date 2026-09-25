#pragma once
// Runtime checks for the concurrency rules in CLAUDE.md.
//
// The firmware's event loops are each one task draining one queue. A handler runs
// on that task, so while it executes the queue is not being drained and no other
// handler on that loop runs. "In dispatch" below means exactly that: there is a
// handler frame below you on the current task's stack.
//
// Three things are then forbidden, and the shims report them here instead of
// aborting, so one run surfaces every violation rather than just the first:
//
//   BlockingInDispatch      vTaskDelay(), or a semaphore take with a non-zero
//                           timeout, while a handler is running. This stalls
//                           dispatch. On the default loop that means
//                           WIFI_EVENT_STA_DISCONNECTED is never delivered and
//                           esp_wifi_connect() is never called -- the PR #26
//                           deadlock.
//
//   BlockingPostInDispatch  a non-zero-timeout post from inside a handler to a
//                           loop OTHER than the one being dispatched. ESP-IDF
//                           clamps a post to your *own* loop down to a timeout of
//                           0 (esp_event.c, "The loop has a dedicated task"), so
//                           self-posting is safe by construction; a cross-loop
//                           post is not clamped and can park the dispatching task
//                           on someone else's queue.
//
//   BlockingPost            a non-zero-timeout post from an ordinary task. The
//                           caller becomes a waiter on the shared 32-slot default
//                           queue and may be woken ahead of the Wi-Fi task's own
//                           post, dropping it. Post with a timeout of 0 and back
//                           off in the task's own vTaskDelay() instead.
//
// Tests normally just call arch::expect_clean() at the end of main().

#include "esp_event_base.h"
#include "freertos/FreeRTOS.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace arch {

enum class Rule {
  BlockingInDispatch,
  BlockingPostInDispatch,
  BlockingPost,
};

const char *name(Rule rule);

struct Violation {
  Rule rule;
  std::string call;    // the offending call, e.g. "vTaskDelay(500)"
  std::string context; // where it happened, e.g. "handler NEXTION_EVENT:7 on loop 'nextion_uart'"
};

// ---- test-facing API --------------------------------------------------------

const std::vector<Violation> &violations();
void reset();
void print(std::FILE *out = stderr);

// Print any violations and return the count, for use as a test's exit status.
int expect_clean();

// ---- shim-facing API --------------------------------------------------------
//
// enter_dispatch()/exit_dispatch() bracket a handler call; everything else asks
// about, or reports against, the innermost frame.

void enter_dispatch(const void *loop, const char *loop_name, esp_event_base_t base, int32_t id, const void *handler);
void exit_dispatch();

bool in_dispatch();
const void *current_dispatch_loop();

// A call that parks the current task. `ticks` of 0 never blocks and is always fine.
void check_blocking_call(const char *call, TickType_t ticks);

// A post to `target_loop` (nullptr meaning the default loop) with `ticks` timeout.
void check_post(const void *target_loop, esp_event_base_t base, int32_t id, TickType_t ticks);

} // namespace arch
