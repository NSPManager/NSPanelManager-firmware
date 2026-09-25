// Host unit test for RoomManager room navigation, built against the real
// lib/RoomManager/RoomManager.cpp with shimmed ESP-IDF and faked collaborators.
#include <RoomManager.hpp>
#include <arch_rules.hpp>
#include <protobuf_nspanel.pb-c.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace fake {
extern std::vector<std::string> mqtt_subscribed;
extern std::vector<std::string> mqtt_unsubscribed;
extern std::shared_ptr<NSPanelConfig> config;
} // namespace fake

static int g_failures = 0;
#define CHECK_EQ(actual, expected)                                                        \
  do {                                                                                    \
    auto _a = (actual);                                                                   \
    auto _e = (expected);                                                                 \
    if (_a != _e) {                                                                       \
      std::printf("  FAIL %s:%d: %s == %s (got %lld, want %lld)\n", __FILE__, __LINE__,    \
                  #actual, #expected, (long long)_a, (long long)_e);                       \
      ++g_failures;                                                                       \
    }                                                                                     \
  } while (0)

// Build a config with the given room ids and default room.
static std::shared_ptr<NSPanelConfig> make_config(std::vector<uint32_t> room_ids, uint32_t default_room) {
  auto cfg = std::shared_ptr<NSPanelConfig>(new NSPanelConfig(), [](NSPanelConfig *c) {
    for (size_t i = 0; i < c->n_room_infos; i++) delete c->room_infos[i];
    delete[] c->room_infos;
    delete c;
  });
  nspanel_config__init(cfg.get());
  cfg->n_room_infos = room_ids.size();
  cfg->room_infos = new NSPanelConfig__RoomInfo *[room_ids.size()];
  for (size_t i = 0; i < room_ids.size(); i++) {
    cfg->room_infos[i] = new NSPanelConfig__RoomInfo();
    nspanel_config__room_info__init(cfg->room_infos[i]);
    cfg->room_infos[i]->room_id = room_ids[i];
  }
  cfg->default_room = default_room;
  return cfg;
}

static void test_next_room_advances_in_order() {
  std::printf("test_next_room_advances_in_order\n");
  fake::config = make_config({3, 7, 9}, 3);
  RoomManager::go_to_room_id(3);
  CHECK_EQ(RoomManager::get_current_room_id(), 3u);
  RoomManager::go_to_next_room();
  CHECK_EQ(RoomManager::get_current_room_id(), 7u);
  RoomManager::go_to_next_room();
  CHECK_EQ(RoomManager::get_current_room_id(), 9u);
}

static void test_next_room_wraps_to_first() {
  std::printf("test_next_room_wraps_to_first\n");
  fake::config = make_config({3, 7, 9}, 3);
  RoomManager::go_to_room_id(9);
  RoomManager::go_to_next_room();
  CHECK_EQ(RoomManager::get_current_room_id(), 3u);
}

static void test_previous_room_wraps_to_last() {
  std::printf("test_previous_room_wraps_to_last\n");
  fake::config = make_config({3, 7, 9}, 3);
  RoomManager::go_to_room_id(3);
  RoomManager::go_to_previous_room();
  CHECK_EQ(RoomManager::get_current_room_id(), 9u);
}

static void test_unknown_room_falls_back_to_default() {
  std::printf("test_unknown_room_falls_back_to_default\n");
  fake::config = make_config({3, 7, 9}, 7);
  RoomManager::go_to_room_id(42); // not in config
  CHECK_EQ(RoomManager::get_current_room_id(), 7u);
}

static void test_navigation_resubscribes_mqtt_topics() {
  std::printf("test_navigation_resubscribes_mqtt_topics\n");
  fake::config = make_config({3, 7}, 3);
  RoomManager::go_to_room_id(3);
  fake::mqtt_subscribed.clear();
  fake::mqtt_unsubscribed.clear();

  RoomManager::go_to_next_room();

  CHECK_EQ(fake::mqtt_unsubscribed.size(), 1u);
  CHECK_EQ(fake::mqtt_subscribed.size(), 1u);
  if (!fake::mqtt_subscribed.empty()) {
    const std::string expected = "nspanel/mqttmanager_10.0.0.5/room/7/state";
    if (fake::mqtt_subscribed[0] != expected) {
      std::printf("  FAIL: subscribed to '%s', want '%s'\n", fake::mqtt_subscribed[0].c_str(), expected.c_str());
      ++g_failures;
    }
  }
}

static void test_empty_config_is_rejected() {
  std::printf("test_empty_config_is_rejected\n");
  fake::config = make_config({}, 0);
  CHECK_EQ(RoomManager::go_to_next_room(), ESP_ERR_NOT_FINISHED);
}

// A known violation, also recorded in tools/event_post_timeouts.baseline:
// replace_home_page_status() posts HOME_PAGE_UPDATED onto the shared 32-slot
// default loop with a 250 ms timeout, where CLAUDE.md requires a timeout of 0
// plus back-off in the caller's own vTaskDelay(). Asserted rather than ignored
// so the harness is shown to catch real firmware code, and so this becomes a
// regression test the moment the call is fixed.
static void test_replace_home_page_status_posts_with_a_blocking_timeout() {
  std::printf("test_replace_home_page_status_posts_with_a_blocking_timeout\n");
  arch::reset();

  RoomManager::replace_home_page_status(nullptr);

  const std::vector<arch::Violation> &found = arch::violations();
  if (found.size() != 1 || found[0].rule != arch::Rule::BlockingPost) {
    std::printf("  FAIL: expected one blocking post, got %zu violation(s).\n"
                "        If this call was fixed, drop this test and refresh the\n"
                "        lint baseline with `make lint-baseline`.\n",
                found.size());
    arch::print(stdout);
    ++g_failures;
  }
  arch::reset();
}

int main() {
  RoomManager::init();
  test_next_room_advances_in_order();
  test_next_room_wraps_to_first();
  test_previous_room_wraps_to_last();
  test_unknown_room_falls_back_to_default();
  test_navigation_resubscribes_mqtt_topics();
  test_empty_config_is_rejected();
  test_replace_home_page_status_posts_with_a_blocking_timeout();
  // Whatever else these tests assert, the code under test must not have broken
  // the concurrency rules in CLAUDE.md while doing it.
  g_failures += arch::expect_clean();

  std::printf("\n%s\n", g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
  return g_failures == 0 ? 0 : 1;
}
