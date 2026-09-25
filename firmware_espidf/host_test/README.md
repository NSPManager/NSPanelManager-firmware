# Host (x86) test harness — proof of concept

Runs firmware logic as an ordinary Linux binary. No ESP32, no QEMU, no ESP-IDF build.

```
make              # build and run the tests, then lint
make lint         # esp_event_post() timeout check, fails only on NEW violations
make lint-all     # list every violation, baselined or not
make lint-baseline# re-record the baseline after fixing some
make compile-all  # report which firmware .cpp files compile on the host
make clean
```

## How it works

Three pieces, in increasing order of how much they know about the firmware:

- `shims/` — stand-ins for the ESP-IDF headers the firmware includes. Mostly type and
  prototype declarations. `host_shims.cpp` gives working implementations of the few
  primitives tests actually depend on: FreeRTOS mutexes (backed by `std::timed_mutex`)
  and the esp_event loop (dispatched **synchronously** on the posting thread, so tests
  are deterministic). Peripheral calls are declared but not defined — a test that
  reaches one fails to link, which is the signal you wanted.
- `fakes/` — replacements for whole collaborator classes, substituted at link time by
  simply not linking the real `.cpp`. Because every manager is a static class with a
  stable public API, a fake is a few dozen lines and needs no change to firmware code.
  Fakes record what they were asked to do (`fake::mqtt_subscribed`, …) so tests can assert on it.
- `tests/` — the tests themselves, linking the real firmware translation unit under test.

## Architecture rules

`shims/arch_rules.cpp` turns the concurrency rules in CLAUDE.md into checks that run
during any test. Because every blocking call the firmware can make already passes
through the shims, no firmware source changes and no dependency injection are needed.

An event loop is one task draining one queue, so while a handler runs the queue is
not drained and no other handler on that loop runs. **In dispatch** means exactly
that: a handler frame is below you on the current task's stack. Three things are
then forbidden:

| Rule | What trips it |
|---|---|
| `BlockingInDispatch` | `vTaskDelay()`, or a semaphore take with a non-zero timeout, inside a handler. On the default loop this stalls delivery of `WIFI_EVENT_STA_DISCONNECTED` — the PR #26 deadlock. |
| `BlockingPostInDispatch` | a non-zero-timeout post from inside a handler to a loop *other* than the one being dispatched. |
| `BlockingPost` | a non-zero-timeout post onto the shared 32-slot default queue from an ordinary task, where the caller can be woken ahead of the Wi-Fi task's own post. |

Posting to the loop you are *currently dispatching on* is not a violation: ESP-IDF
clamps that case to a timeout of 0 (`esp_event.c`, "The loop has a dedicated task"),
so it cannot block. The shim emulates the clamp, and defers such a post until the
current handler returns rather than running it inline, which is what the device does.

Violations are collected rather than fatal, so one run surfaces all of them, and a
report names the handler it happened in:

```
1 architecture rule violation(s):
  blocking call inside a handler
    vTaskDelay
    in handler Nextion::_uart_data_handler dispatching NEXTION_EVENT:7 on loop 'nextion_uart'
```

Naming the handler needs `-rdynamic`; firmware handlers are static class members, so
they resolve. Run with `ARCH_RULES=abort` to stop at the first violation with the
offending call still on the stack.

A test opts in by ending `main()` with:

```cpp
g_failures += arch::expect_clean();
```

### Static check

The rule "post with a timeout of `0`" is visible in the source, so
`tools/lint_event_posts.py` checks it across the whole firmware rather than only the
paths tests reach. There are **38** such calls today (32 onto the shared default
loop); they are recorded in `tools/event_post_timeouts.baseline` so `make lint`
fails only on *new* ones. It also fails when a baselined call is fixed, to prompt
`make lint-baseline`.

Nothing under `lib/`, `src/` or `include/` is modified, and none of this is in the
PlatformIO build, so the firmware binary is unaffected.

## Status

Tests: `test_arch_rules` (17 cases, covering the checker itself) and
`test_room_navigation` (7 cases against the real `RoomManager.cpp`).

18 of 19 firmware translation units compile on the host unchanged. `WiFiManager.cpp`
does not: it uses the esp_netif/DHCP/captive-portal API directly and needs the real
`espp::DnsServer`. It is also the least valuable unit to test in isolation.
