// Tests for the architecture-rule checks in shims/arch_rules.cpp.
//
// These exercise the checker itself against handlers written to be good or bad
// on purpose. No firmware translation unit is linked -- what is under test is
// the harness, not the panel.
#include <arch_rules.hpp>
#include <esp_event.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdio>
#include <string>
#include <vector>

ESP_EVENT_DEFINE_BASE(TEST_EVENT);

enum test_event_t {
  EVENT_A = 1,
  EVENT_B = 2,
};

static int g_failures = 0;

#define CHECK(cond)                                                                     \
  do {                                                                                  \
    if (!(cond)) {                                                                      \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                      \
      ++g_failures;                                                                     \
    }                                                                                   \
  } while (0)

// Assert on the violations produced by `body`, then clear them.
static void expect(const char *what, void (*body)(), std::vector<arch::Rule> want) {
  std::printf("%s\n", what);
  arch::reset();
  body();
  const std::vector<arch::Violation> &got = arch::violations();
  if (got.size() != want.size()) {
    std::printf("  FAIL: %zu violation(s), want %zu\n", got.size(), want.size());
    arch::print(stdout);
    ++g_failures;
    arch::reset();
    return;
  }
  for (size_t i = 0; i < want.size(); i++) {
    if (got[i].rule != want[i]) {
      std::printf("  FAIL: violation %zu is '%s', want '%s'\n", i, arch::name(got[i].rule),
                  arch::name(want[i]));
      ++g_failures;
    }
  }
  arch::reset();
}

// ---- loops and handlers -----------------------------------------------------

static esp_event_loop_handle_t g_other_loop = nullptr;
static SemaphoreHandle_t g_mutex = nullptr;
static std::vector<std::string> g_order;

static void handler_clean(void *, esp_event_base_t, int32_t, void *) {
  g_order.push_back("clean");
}

// Every firmware handler is a static class member, as here. That has external
// linkage, so arch_rules can name it in a violation report; the internal-linkage
// free functions below cannot be named and report an address instead.
struct FakeManager {
  static void handler_delays(void *, esp_event_base_t, int32_t, void *) {
    vTaskDelay(pdMS_TO_TICKS(1));
  }
};

static void handler_delays(void *, esp_event_base_t, int32_t, void *) {
  vTaskDelay(pdMS_TO_TICKS(1));
}

static void handler_yields(void *, esp_event_base_t, int32_t, void *) {
  vTaskDelay(0); // a yield, not a block
}

// A blocking call several frames below the handler is still inside dispatch.
static void deep_helper() { vTaskDelay(pdMS_TO_TICKS(1)); }
static void middle_helper() { deep_helper(); }
static void handler_delays_indirectly(void *, esp_event_base_t, int32_t, void *) {
  middle_helper();
}

static void handler_takes_mutex_blocking(void *, esp_event_base_t, int32_t, void *) {
  if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(250)) == pdTRUE) {
    xSemaphoreGive(g_mutex);
  }
}

static void handler_takes_mutex_nonblocking(void *, esp_event_base_t, int32_t, void *) {
  if (xSemaphoreTake(g_mutex, 0) == pdTRUE) {
    xSemaphoreGive(g_mutex);
  }
}

// The Nextion::_uart_data_handler shape: runs on a dedicated loop, posts to the
// default one with a long timeout.
static void handler_posts_cross_loop_blocking(void *, esp_event_base_t, int32_t, void *) {
  esp_event_post(TEST_EVENT, EVENT_B, NULL, 0, pdMS_TO_TICKS(5000));
}

static void handler_posts_cross_loop_nonblocking(void *, esp_event_base_t, int32_t, void *) {
  esp_event_post(TEST_EVENT, EVENT_B, NULL, 0, 0);
}

// Posting to your own loop: ESP-IDF clamps the timeout to 0, so this is safe
// however large the timeout looks.
static void handler_posts_self_blocking(void *, esp_event_base_t, int32_t, void *) {
  esp_event_post_to(g_other_loop, TEST_EVENT, EVENT_B, NULL, 0, pdMS_TO_TICKS(5000));
}

static void handler_posts_self_once(void *, esp_event_base_t, int32_t, void *) {
  static bool posted = false;
  g_order.push_back("first:enter");
  if (!posted) {
    posted = true;
    esp_event_post_to(g_other_loop, TEST_EVENT, EVENT_B, NULL, 0, 0);
  }
  g_order.push_back("first:exit");
}

static void handler_records_second(void *, esp_event_base_t, int32_t, void *) {
  g_order.push_back("second");
}

static uint32_t g_seen_payload = 0;
static void handler_records_payload(void *, esp_event_base_t, int32_t, void *data) {
  g_seen_payload = (data != nullptr) ? *static_cast<uint32_t *>(data) : 0;
}

static void handler_posts_payload_to_self(void *, esp_event_base_t, int32_t, void *) {
  uint32_t local = 0xAABBCCDD; // dies when this handler returns
  esp_event_post_to(g_other_loop, TEST_EVENT, EVENT_B, &local, sizeof(local), 0);
}

// ---- helpers ----------------------------------------------------------------

static void on_other(int32_t id, esp_event_handler_t fn) {
  esp_event_handler_register_with(g_other_loop, TEST_EVENT, id, fn, nullptr);
}
static void off_other(int32_t id, esp_event_handler_t fn) {
  esp_event_handler_unregister_with(g_other_loop, TEST_EVENT, id, fn);
}
static void fire_other() { esp_event_post_to(g_other_loop, TEST_EVENT, EVENT_A, NULL, 0, 0); }

// Run `fn` as a handler on the dedicated loop and post EVENT_A to it.
template <esp_event_handler_t fn> static void in_handler() {
  on_other(EVENT_A, fn);
  fire_other();
  off_other(EVENT_A, fn);
}

// ---- tests ------------------------------------------------------------------

static void test_no_violations_outside_dispatch() {
  std::printf("test_no_violations_outside_dispatch\n");
  arch::reset();
  CHECK(!arch::in_dispatch());
  vTaskDelay(pdMS_TO_TICKS(1));                        // fine: an ordinary task
  xSemaphoreTake(g_mutex, pdMS_TO_TICKS(250));
  xSemaphoreGive(g_mutex);
  CHECK(arch::violations().empty());
  arch::reset();
}

static void test_dispatch_flag_tracks_handlers() {
  std::printf("test_dispatch_flag_tracks_handlers\n");
  static bool saw_in_dispatch = false;
  saw_in_dispatch = false;
  auto probe = [](void *, esp_event_base_t, int32_t, void *) { saw_in_dispatch = arch::in_dispatch(); };
  CHECK(!arch::in_dispatch());
  on_other(EVENT_A, probe);
  fire_other();
  off_other(EVENT_A, probe);
  CHECK(saw_in_dispatch);
  CHECK(!arch::in_dispatch()); // and unwound afterwards
}

static void test_deferred_self_post_runs_after_the_handler_returns() {
  std::printf("test_deferred_self_post_runs_after_the_handler_returns\n");
  arch::reset();
  g_order.clear();
  on_other(EVENT_A, handler_posts_self_once);
  on_other(EVENT_B, handler_records_second);
  fire_other();
  off_other(EVENT_A, handler_posts_self_once);
  off_other(EVENT_B, handler_records_second);

  const std::vector<std::string> want = {"first:enter", "first:exit", "second"};
  CHECK(g_order == want);
  if (g_order != want) {
    for (const std::string &s : g_order) std::printf("    got %s\n", s.c_str());
  }
  arch::reset();
}

static void test_deferred_payload_is_copied() {
  std::printf("test_deferred_payload_is_copied\n");
  arch::reset();
  g_seen_payload = 0;
  on_other(EVENT_A, handler_posts_payload_to_self);
  on_other(EVENT_B, handler_records_payload);
  fire_other();
  off_other(EVENT_A, handler_posts_payload_to_self);
  off_other(EVENT_B, handler_records_payload);
  CHECK(g_seen_payload == 0xAABBCCDDu);
  arch::reset();
}

int main() {
  g_mutex = xSemaphoreCreateMutex();
  esp_event_loop_args_t args = {};
  args.task_name = "test_loop";
  esp_event_loop_create(&args, &g_other_loop);

  test_no_violations_outside_dispatch();
  test_dispatch_flag_tracks_handlers();

  expect("test_clean_handler_is_clean", [] { in_handler<handler_clean>(); }, {});
  expect("test_yield_in_handler_is_clean", [] { in_handler<handler_yields>(); }, {});
  expect("test_delay_in_handler_is_caught", [] { in_handler<handler_delays>(); },
         {arch::Rule::BlockingInDispatch});
  expect("test_delay_below_the_handler_is_caught", [] { in_handler<handler_delays_indirectly>(); },
         {arch::Rule::BlockingInDispatch});
  expect("test_blocking_mutex_in_handler_is_caught",
         [] { in_handler<handler_takes_mutex_blocking>(); }, {arch::Rule::BlockingInDispatch});
  expect("test_try_take_in_handler_is_clean",
         [] { in_handler<handler_takes_mutex_nonblocking>(); }, {});

  expect("test_cross_loop_blocking_post_in_handler_is_caught",
         [] { in_handler<handler_posts_cross_loop_blocking>(); },
         {arch::Rule::BlockingPostInDispatch});
  expect("test_cross_loop_nonblocking_post_in_handler_is_clean",
         [] { in_handler<handler_posts_cross_loop_nonblocking>(); }, {});
  expect("test_self_post_in_handler_is_clean_whatever_the_timeout",
         [] { in_handler<handler_posts_self_blocking>(); }, {});

  expect("test_blocking_post_to_default_loop_is_caught",
         [] { esp_event_post(TEST_EVENT, EVENT_A, NULL, 0, pdMS_TO_TICKS(250)); },
         {arch::Rule::BlockingPost});
  expect("test_nonblocking_post_to_default_loop_is_clean",
         [] { esp_event_post(TEST_EVENT, EVENT_A, NULL, 0, 0); }, {});
  expect("test_blocking_post_to_dedicated_loop_is_clean",
         [] { esp_event_post_to(g_other_loop, TEST_EVENT, EVENT_A, NULL, 0, pdMS_TO_TICKS(250)); },
         {});

  test_deferred_self_post_runs_after_the_handler_returns();
  test_deferred_payload_is_copied();

  // A violation names the handler it happened in, which is what makes the report
  // actionable; check that against the shape every firmware handler has.
  std::printf("test_violation_names_the_handler\n");
  arch::reset();
  in_handler<&FakeManager::handler_delays>();
  CHECK(arch::violations().size() == 1);
  if (arch::violations().size() == 1) {
    const std::string &context = arch::violations()[0].context;
    CHECK(context.find("FakeManager::handler_delays") != std::string::npos);
    CHECK(context.find("on loop 'test_loop'") != std::string::npos);
    std::printf("\nsample violation report:\n");
    arch::print(stdout);
  }
  arch::reset();

  std::printf("\n%s\n", g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
  return g_failures == 0 ? 0 : 1;
}
