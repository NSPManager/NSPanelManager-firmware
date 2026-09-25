# Host-side testing for the NSPanel firmware

Status: **exploration / proof of concept.** Not reviewed, not adopted, not wired into CI.
Written 2026-09-19, updated 2026-09-25 when the architecture rules were added.

## The question

Can we get meaningful automated tests for this firmware without flashing an ESP32?
Two candidate routes were evaluated:

1. Build the firmware against ESP-IDF's own `linux` target (the POSIX simulator).
2. Compile the firmware's `.cpp` files on the host against stand-in ESP-IDF headers,
   and replace collaborators at link time.

Route 2 won. Route 1 is blocked for this codebase — details below.

## Route 1: the ESP-IDF `linux` target

Findings are from reading component `CMakeLists.txt` files in the installed IDF
(`~/.platformio/packages/framework-espidf@3.50500.0`, IDF 5.5.0), not from docs.

### What genuinely runs on x86

| Component | How |
|---|---|
| `freertos` | real POSIX port (`FreeRTOS-Kernel/portable/linux`) |
| `esp_event` | real |
| `log` | real |
| `esp_partition` | real, file-backed |
| `nvs_flash` | real |
| `esp_http_client` | real — has an explicit `else() set(req linux esp_event)` branch |
| `tcp_transport`, `esp-tls` | real |

### What exists only as a CMock mock

`tools/mocks/` provides 15 mocked components, including `esp_event`, `freertos`,
`esp_timer`, `esp_wifi`, `esp_netif`, `driver`, `esp_partition`, `spi_flash`,
`lwip`, `tcp_transport`, `esp-tls`, `bootloader_support`, `esp_hw_support`,
`http_parser`.

### Hard blockers

These components call `return()` before registering anything on the `linux` target,
so they cannot be configured at all:

- **`app_update`**
- **`esp_https_ota`**
- **`driver`** (the legacy driver component)

**Consequence: `UpdateManager` cannot be built on the linux target.** It is the OTA
state machine and the source of several recent fix branches — i.e. exactly the code
we most want under test. That alone sinks route 1.

### Silent gaps (configure fine, do nothing)

- `esp_timer` registers headers only — no implementation.
- `esp_driver_uart`, `esp_driver_i2c`, `esp_adc` compile **zero sources**, because
  `soc/linux/include/soc/soc_caps.h` defines no `SOC_UART_SUPPORTED`,
  `SOC_I2C_SUPPORTED` or `SOC_ADC_SUPPORTED`. There are no CMock mocks for the newer
  `esp_driver_*` components either.
- The FreeRTOS mock covers `task` / `queue` / `event_groups` — **not `semphr`**.
  `RoomManager.cpp` alone calls `xSemaphoreTake`/`Give` ~30 times.
- `esp_http_server` pulls in mbedtls + lwip.
- `joltwallet/littlefs` and `espp/dns_server` are not linux-aware at all.

### Practical blocker

PlatformIO derives `IDF_TARGET` from the board's MCU
(`espidf.py:61`, `idf_variant = mcu.lower()`), so `pio` cannot select the `linux`
target. There is no `idf.py` or `IDF_PATH` on this machine either. Route 1 would mean
maintaining a second, parallel build system.

## Route 2: host shims + link-time fakes (what was built)

Location: **`firmware_espidf/host_test/`** in the worktree
`/home/james/nspanel/worktrees/testability` (see "Where this lives" below).

Three layers:

| Layer | Path | Size | What it does |
|---|---|---|---|
| Shims | `shims/*.h` | 624 lines of headers | Stand-in ESP-IDF headers — types and prototypes only |
| Impl | `shims/host_shims.cpp` | 186 lines | Working host implementations of the few APIs tests actually exercise |
| Rules | `shims/arch_rules.{hpp,cpp}` | 249 lines | Checks the CLAUDE.md concurrency rules during any test — see below |
| Fakes | `fakes/fake_collaborators.cpp` | 44 lines | Replaces `MqttManager` / `NSPM_ConfigManager` at link time, with spy state |
| Tests | `tests/test_room_navigation.cpp` | 148 lines | 7 tests against the **real** `lib/RoomManager/RoomManager.cpp` |
| Tests | `tests/test_arch_rules.cpp` | 272 lines | 17 tests covering the rule checker itself |
| Lint | `tools/lint_event_posts.py` | 146 lines | Static half of the rules, over the whole firmware |

Build: `Makefile` (89 lines). No CMake, no IDF, no PlatformIO.

### Key design decisions

- **Zero IDF include directories.** Mixing real IDF headers with shims caused cascading
  failures (`esp_task.h` → `sdkconfig.h` → `freertos/FreeRTOSConfig.h`). The shims are
  self-contained. The only real IDF paths on the include line are the portable C
  sources: `protobuf-c` and `cJSON`.
- **`esp_event` dispatches synchronously on the posting thread.** Deterministic tests.
  The trade-off is stated under Caveats.
- **FreeRTOS semaphores are backed by `std::timed_mutex`** — real mutual exclusion,
  real timeouts.
- **`ESP_LOGx` are no-ops**: `#define ESP_LOGE(tag, fmt, ...) do { } while (0)`.
- **Peripheral functions are declared but never defined.** A test that reaches hardware
  fails to *link*. That is the intended signal, not an accident.
- **Link-time seam, not dependency injection.** A collaborator is replaced by simply not
  linking its real `.cpp`. This is what avoids the invasive refactor — **no firmware
  source file was modified.**
- **C sources build as C.** `gcc -std=c11` for protobuf-c and cJSON, `g++ -std=c++2a`
  for everything else. Compiling the generated `.c` as C++ fails
  (`invalid conversion from 'void*' to 'uint8_t*'`).
- `-std=c++2a`, not `-std=c++20` — host g++ is 9.4.0.

## Architecture rules

The shims are the chokepoint for every blocking call the firmware can make, so the
rules in CLAUDE.md can be enforced there without touching firmware source and without
a dependency-injection refactor.

An event loop is one task draining one queue (`esp_event.c:98`, `:629`, `:676`), so
while a handler runs that queue is not drained and no other handler on that loop runs.
**In dispatch** means exactly that: a handler frame is below you on the current task's
stack. Three things are then forbidden:

| Rule | What trips it |
|---|---|
| `BlockingInDispatch` | `vTaskDelay()`, or a semaphore take with a non-zero timeout, inside a handler. On the default loop this stalls delivery of `WIFI_EVENT_STA_DISCONNECTED`, so `esp_wifi_connect()` is never called — the PR #26 deadlock. |
| `BlockingPostInDispatch` | a non-zero-timeout post from inside a handler to a loop *other* than the one being dispatched. |
| `BlockingPost` | a non-zero-timeout post onto the shared 32-slot default queue from an ordinary task, where the caller can be woken ahead of the Wi-Fi task's own post. |

Violations are collected rather than fatal, so one run surfaces all of them, and each
names the handler it happened in (`dladdr` + `-rdynamic`; firmware handlers are static
class members, so they have external linkage and resolve):

```
1 architecture rule violation(s):
  blocking call inside a handler
    vTaskDelay
    in handler Nextion::_uart_data_handler dispatching NEXTION_EVENT:7 on loop 'nextion_uart'
```

A test opts in with `g_failures += arch::expect_clean();` at the end of `main()`.
`ARCH_RULES=abort` stops at the first violation with the call still on the stack.

### What ESP-IDF already guards, and what it does not

`esp_event_post_to` clamps a post to the loop you are *currently dispatching on* down
to a timeout of 0 (`esp_event.c:962`, "The loop has a dedicated task"). It has to:
you are the only task that can drain that queue, so waiting on it would self-deadlock.
**Self-posting is therefore safe by construction, however large the timeout looks —
and flagging it would be a false positive.**

That check is `loop->task != xTaskGetCurrentTaskHandle()`, i.e. **per loop**. A post
from a handler to a *different* loop is not clamped. `Nextion::_uart_data_handler`
(`Nextion.cpp:264`) is registered on the dedicated uart loop (`:94`) and posts to the
default loop with a 5000 ms timeout (`:319`, `:323`), which is exactly that shape.

The shim now emulates the clamp, and defers a self-post until the current handler
returns rather than running it inline, which is what the device does. Deferred
payloads are copied, as `esp_event` does, because the posting handler's frame is gone
by the time they are delivered.

### Static half

The "post with a timeout of `0`" rule is visible in the source, so
`tools/lint_event_posts.py` checks it across the whole firmware rather than only the
paths tests reach. **38 calls pass a non-zero timeout today — 32 of them onto the
shared default loop**, against a documented rule that says to use `0` and back off in
the caller's own `vTaskDelay()`. They are baselined in
`tools/event_post_timeouts.baseline`, so `make lint` fails only on *new* ones (and on
a baselined call being fixed, to prompt `make lint-baseline`).

Dedicated-loop posts are reported separately and are much less dangerous: that queue
is private and is not what Wi-Fi contends for.

## Results

- **7/7 tests pass** against the real `RoomManager.cpp`; **17/17** for the rule checker.
- **18 of 19 firmware translation units compile unchanged on x86.**
- **The rules catch real firmware code, not just synthetic tests.**
  `RoomManager::replace_home_page_status` posts `HOME_PAGE_UPDATED` onto the default
  loop with a 250 ms timeout; `test_room_navigation` pins that as an expected
  violation. Room navigation itself is clean — verified to be genuinely clean rather
  than the check being inert.
- **Mutation test passed.** Changing `room_infos[0]` to `room_infos[1]` in
  `RoomManager.cpp` produced:
  `FAIL ...:64: RoomManager::get_current_room_id() == 3u (got 7, want 3)`
  The tests do catch regressions; they are not vacuous.
- **No firmware impact.** `pio run -e original_sonoff` → `SUCCESS 00:06:44`,
  RAM 13.7% (44964/327680), Flash 86.3% (1526893/1769472). LDF reports
  "Found 16 compatible libraries" with `host_test` absent from the dependency graph.

### The one TU that does not compile

`lib/WiFiManager/WiFiManager.cpp` — it uses `esp_wifi_init`, `WIFI_EVENT`, `IP_EVENT`,
`esp_netif_create_default_wifi_sta/ap`, `esp_netif_get_handle_from_ifkey`,
`esp_netif_set_hostname`, which the shims never declare. This is **shim incompleteness,
not a fundamental blocker** — adding the prototypes would fix it. (`WiFiManager.hpp`
already compiles fine; it is included by TUs that pass.)

Rebasing onto current `devel` produced a second, instructive failure: PR #35 added
`_mqtt_config.network.timeout_ms`, which the `mqtt_client.h` shim did not declare, so
`MqttManager.cpp` stopped compiling. One line fixed it — but it is a live example of
the shim-drift caveat below, and of why `make compile-all` is worth running.

Note `WiFiManager.hpp:4` includes `<dns_server.hpp>` solely for
`static inline espp::DnsServer *_dns_server = nullptr;` at line 72, and the header
references `wifi_ap_record_t`, `esp_netif_ip_info_t`, `TimerHandle_t`,
`wifi_init_config_t` and `wifi_config_t` without including their headers.

## How to run it

```bash
cd firmware_espidf/host_test
make               # build and run the tests, then lint
make lint          # esp_event_post() timeout check; fails only on NEW violations
make lint-all      # list every violation, baselined or not
make lint-baseline # re-record the baseline after fixing some
make compile-all   # the 18-of-19 sweep
make clean
```

Reading order: `README.md` → `shims/arch_rules.hpp` (the rules, with the reasoning) →
`tests/test_arch_rules.cpp` → `tests/test_room_navigation.cpp` →
`fakes/fake_collaborators.cpp` → `shims/host_shims.cpp` → `Makefile`. The remaining
30 files in `shims/` are typedefs and prototypes — skimmable, not worth close reading.

## Caveats

- **Shims type-check; they do not guarantee behavioural fidelity.** Concrete example
  already hit: IDF's FreeRTOS fork has `pcTaskGetName` returning `char *`, while upstream
  FreeRTOS returns `const char *`. The shim had to be corrected to match IDF. Other
  such drifts will exist and will not announce themselves.
- **The harness is still single-threaded.** `xTaskCreate` records the call and returns
  `pdPASS` without running the task body, and dispatch is synchronous on the posting
  thread. The architecture rules work anyway — they are about *structure*, and a
  thread-local dispatch flag is correct with or without real tasks — but genuine
  scheduling bugs remain out of reach. See "Testing scheduling and races" below.
- **The rules prove violations, not the absence of deadlock.** A handler that blocks on
  something the shims do not model still slips through, and the dynamic check only
  covers paths a test actually reaches. That is what the static lint is for.
- **`portENTER_CRITICAL` maps to one global `recursive_mutex`**, so the harness
  over-serialises relative to the device's per-instance spinlocks and can say nothing
  about lock granularity. This must be fixed before ThreadSanitizer results mean much.
- Host GCC 9.4 vs xtensa GCC 15.2 — a real language-version gap.
- `managed_components/mittelab__nlohmann-json` FetchContent-downloads `json_impl.hpp`
  from GitHub at configure time. It was copied by hand from
  `~/.cache/Espressif/ComponentManager/service_d92d8f1e/mittelab__nlohmann-json_3.11.3_9a48e24d/nlohmann/json_impl.hpp`.
  **This needs solving before CI** — a build that reaches the network at configure time
  is not reproducible.
- **There is no CI in this repository at all.** `firmware_espidf/test/` is empty.

## Testing scheduling and races

Worth separating two bug classes, because they need different tools and most of what
has actually bitten this project is the first:

| Class | Example | Nature |
|---|---|---|
| **Protocol violations** | blocking inside a handler (PR #26); posting with a non-zero timeout onto the loop you depend on | **Deterministic.** Reproduce every time, given the right harness. |
| **True data races** | unsynchronised access to the 365 `static inline` members across 44 registered handlers and 20 task entry points | Genuinely nondeterministic. Needs a race detector. |

`46f2bc8` is in the first class. It is not a race at all; it is a structural property —
a handler that blocks stalls the dispatcher that feeds it — which is why the rule
checks above can address it without any concurrency in the harness.

Options for the rest, ranked:

1. **Borrow the real FreeRTOS POSIX port and the real `esp_event`.** Not the whole IDF
   `linux` target, just two pieces of it, both portable:
   `freertos/FreeRTOS-Kernel/portable/linux/port.c` emulates a *single core* with real
   priorities (non-running tasks block in `sigwait()`, tick via `SIGALRM`), which is
   much closer to the device than free-running pthreads; and `esp_event`'s three
   sources build identically on every target — on `linux` the CMakeLists only swaps
   `esp_timer` for the `linux` component. With `queue_size = 32` this turns PR #26 into
   a deterministic regression test: fill the queue, block a handler, assert the Wi-Fi
   post is dropped. Prerequisite: `xTaskCreate` must actually run task bodies.
2. **ThreadSanitizer.** One compiler flag once (1) lands, and the only thing that
   addresses the true-race class. Fix the global-critical-section shim first.
3. **QEMU** (`qemu-system-xtensa`). Real FreeRTOS, real IDF, no hardware — the highest
   fidelity available without a panel. Wi-Fi is not emulated, but the PR #26 class only
   needs a synthetic `WIFI_EVENT_STA_DISCONNECTED` post. A whole second harness, so not
   first.
4. **Cooperative scheduler with explicit interleaving control.** Most powerful, biggest
   lift; largely redundant while (1) already serialises to one core.
5. **TLA+.** Right tool for "is 32 slots enough given N publishers?" — a design
   question. It does not test the code and will drift from it.

Out of reach entirely: real timing, real Wi-Fi disconnects, flash and UART latency, and
priority inversion involving the actual Wi-Fi task (priority 23, against `sys_evt` at
`ESP_TASKD_EVENT_PRIO` = 20). Note also that race tests are inherently flaky — the rule
checks and (1) are deterministic and belong in CI; ThreadSanitizer belongs in a nightly
job, not a gate.

## Recommendation

1. Adopt shims + link-time fakes as the base. It is cheap, it required no production
   code changes, and it already catches regressions.
   - **Done in this branch:** the architecture rules and the `esp_event_post` lint.
     These were the cheapest item on the list and they found 38 violations of a
     documented rule on the first run.
2. Extract the genuinely testable pure logic into free functions and test it:
   - `UpdateManager::update_gui` (`UpdateManager.cpp:32`) is 229 lines mixing HTTP retry,
     chunk math and UART streaming. The **chunk/offset arithmetic** is the valuable part
     — and note the deliberate non-RFC-7233 exclusive-end Range convention (PR #12,
     documented in CLAUDE.md) which must not be "fixed".
   - `Nextion::_uart_data_handler` frame parsing.
3. **Skip the IDF linux target** — blocked on `app_update` / `esp_https_ota`.
4. **Skip a dependency-injection refactor** — that is the genuinely invasive option, and
   the link-time seam makes it unnecessary.

## Where this lives

Branch **`explore/host-tests`**, rebased onto `upstream/devel` at `29a11a0`
("MqttManager: fix permanent MQTT deadlock when a publish fails (#35)"). Two commits:

```
host_test: x86 unit-test harness for firmware logic
host_test: check the CLAUDE.md concurrency rules at runtime and in CI
```

Everything is confined to `firmware_espidf/host_test/` plus this file. Nothing under
`lib/`, `src/` or `include/` is modified, and none of it is in the PlatformIO build.

`firmware_espidf/dependencies.lock` may show as modified in the working tree: the IDF
component manager rewrites it during a `pio run`, and it is deliberately not part of
either commit. `git checkout firmware_espidf/dependencies.lock` to clear it.
