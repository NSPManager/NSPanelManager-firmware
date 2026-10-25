# QEMU firmware tests

These tests boot the real firmware image in
[Espressif's QEMU fork](https://github.com/espressif/qemu) and drive it as a panel would be
driven in a house. The firmware talks to a fake NSPanelManager through a real mosquitto, and a
fake Nextion display stands in for the screen, so tests can touch the screen and assert on what
the panel publishes and draws.

Needs `mosquitto` installed (`apt install mosquitto`). Each test starts its own instance on a
random port, so the system service can stay disabled.

```
firmware_espidf/qemu/run_tests.sh            # build, install QEMU if needed, run everything
firmware_espidf/qemu/run_tests.sh -k touch   # extra args go to pytest
```

CI runs the same script (`.github/workflows/qemu-tests.yml`). The suite takes about 6 minutes,
because every test boots a fresh panel. Per-test console and mosquitto logs land in
`qemu/build/test-logs/`, and CI uploads them as an artifact. So are the screenshots, in each
test's `screens/`: one PNG of every page the test waited for (`wait_for_page()`, taken as the page
appears, so often before the firmware has filled it in), and one of the screen when the test
ended (`NN-end.png`) or failed (`NN-failure.png`, also named on stderr). Brightness (`dim`) is not
applied: a screen the firmware has dimmed to 0 is drawn as if lit.

**Using a panel by hand.** `qemu/live.sh` boots one against the fake manager and serves its
screen in a browser (nextion-tools' viewer): click and drag on it to touch the panel, and the
script prints each command the panel sends the manager. It takes the same environment as
`run_tests.sh`, plus `--http PORT`, `--rotation`, and `--weather` for the weather screensaver.
`tests/test_viewer.py` checks that loop: wake the panel and turn on the lights, through the
viewer alone.

## How it is wired

```
            QEMU -M esp32 (firmware built with [env:qemu])
   UART0 ──────────────► console: captured, crash-checked, saved per test
   UART2 ◄─────────────► FakeNextion       (TCP socket)
   open_eth ── slirp ──► host 10.0.2.2 ──► FaultProxy ──► mosquitto ◄── observer ◄──► FakeManager
                         panel :80 forwarded to Rig.panel.web_port
```

| Piece | File | Role |
|---|---|---|
| `Rig` | `nspm_qemu/rig.py` | One panel and its world. `boot()`, `wake_to_home()`, `http_get()`. |
| `QemuPanel` | `nspm_qemu/panel.py` | Builds the flash and eFuse images, runs QEMU, gives `wait_for_log()`, detects panics. |
| `FakeNextion` | `nspm_qemu/fake_nextion.py` | The display emulator ([nextion-tools](https://github.com/jamesmulcahy/nextion-tools)' `nextion_tools.emu`, pinned in `requirements.txt`) as the NSPanel Manager TFT: models the screen from the panel's commands and injects user input. |
| `FakeManager` | `nspm_qemu/fake_manager.py` | Registration handshake, config, room and entity-page state, decoded commands. |
| `Broker` | `nspm_qemu/broker.py` | A mosquitto per test. A paho client records all traffic, a TCP proxy in front of the panel lets tests cut its connection, and connects and subscriptions are read from mosquitto's log. |

The panel boots already configured. The harness builds the LittleFS partition from `data/` plus
a `config.json` that points MQTT at the broker. The eFuses carry a fixed MAC
(`C0:49:EF:00:00:01`) with a valid CRC, so topics are stable.

## Writing a test

Fixtures (`tests/conftest.py`), each a fresh panel:

- `rig`: not started yet.
- `booted`: registered, configured, idle on the screensaver.
- `home`: woken to the home page.

```python
def test_ceiling_button_turns_on_ceiling_lights(home):
    mark = home.manager.command_mark()

    home.nextion.press(22)  # GUI_HOME_PAGE::button_ceiling_id

    cmd = home.manager.wait_for_command(lambda c: c.WhichOneof("CommandData") == "first_page_turn_on", since=mark)
    assert cmd.first_page_turn_on.affect_lights == Cmd.CEILING_LIGHTS
```

**Simulating input** (`FakeNextion`). A touch is a finger on the screen, and the TFT's own code
(from `gui.HMI`) decides what reaches the firmware, as on a panel:

| Call | What happens |
|---|---|
| `press(id, hold=0.1)` | A finger down on the component (by id or name, on the current page), then up. It reports the edges its `sendkey` asks for, and runs its own and the page's touch-capture code. On the items and lights pages that code decodes gestures, so `hold` must cover at least one 50 ms tick of the gesture timer. |
| `touch(id, pressed)` | Only the finger down, or only the lift. |
| `slide(name, value)` | A finger on the slider at the point for `value`; returns the value it took. |
| `swipe("down")`, `drag(start, end)`, `tap(x, y)`, `touch_down/move/up(x, y)` | Gestures and raw touches. |
| `wake()` | A tap in the middle of the screensaver, whose touch capture prints `0x93`. Touches nothing, and returns False, if no screensaver page is shown. |
| `sleep()` | The current page's `sleep_display` timer runs out now, and its code prints `0x92`. Left alone, it runs out after the screensaver timeout the firmware configures (`home.sleep_display.tim`). |
| `send_touch_event(id, pressed)`, `send_frame(code, ...)` | A bare frame, whatever the HMI would send. |
| `send_string(data)` | A `0x99` string event. |

**The display's speed.** By default the fake executes everything the moment it arrives. Set
`nextion.flow_model = True` to make it behave like the real display, buffer, stalls and lost
commands included, as measured on Office (see nextion-tools' README). The firmware on this
branch has no flow control. Even so, waking to the home page loses nothing (16 commands, at most
237 bytes queued), because the bursts that overflow on a panel (the weather forecast after
`page screensaver`, the boot sequence) need weather data FakeManager does not send.

**The display knows the TFT.** The fake reads the TFT's Nextion project (`gui.HMI`), fetched from
NSPanelManager at a pinned commit (`nspm_qemu/hmi.py`; `NSPM_HMI=<path>` to use another, e.g. one
not pushed yet). It answers as a real display does: a page, component or attribute that does not
exist where it is used fails with `0x1A`; the label without its page prefix (`forDay1.txt`)
exists only while its page is shown; ids are the HMI's; `get` returns the HMI's value until one is
assigned; loading a page resets its local components.

**A rejected instruction fails the test.** On a panel, the firmware runs `bkcmd=0`, so a `0x1A`
is silent: a label is never drawn, and nothing says why. Here every rejection not listed in
`nspm_qemu/display_errors.py` fails the test that caused it. List one there only with the reason,
and remove it when the firmware is fixed. Mark a test `@pytest.mark.display_errors_ok` if it
sends bad instructions on purpose.

**`tests/test_gui_data.py`** checks `lib/Pages/GUI_data.hpp` against the HMI without booting
anything: every page and component the firmware names exists, every id it listens for is a
component that reports touches (itself, or through the page's own `printh 65` code), and every
picture id is in range. Known mismatches are listed in the test, which also fails once one of
them is fixed, so the list stays honest.

**Looking at the screen:** `nextion.screenshot(name)` saves a PNG into the test's `screens/`, and
`nextion.snapshot()` gives the pixels as a numpy array. They draw from the HMI and what the
firmware assigned, as nextion-tools' renderer does (see its README for what is drawn).

**Observing the screen:** `page`, `page_history`, `components["page.comp.attr"]` (what the firmware
assigned), `value("page.comp.attr")` (what the display holds, HMI defaults included),
`system["dim"]`, `commands` (raw instructions), `errors` (rejected instructions), and the waits
`wait_for_page()`, `wait_for_value()`, `wait_for_command()` and `wait_for(predicate)`.

**Observing MQTT:** `broker.wait_for_message(topic_filter, predicate, since=broker.mark())`,
`broker.subscriptions(client)`, `broker.wait_for_subscription(...)`, and
`broker.drop_client(client)` to simulate a network fault. To publish manager-side state, change
`manager.rooms` / `manager.entity_pages` / `manager.config`, then call `publish_room()` /
`publish_entity_page()` / `publish_config()`.

Use the `mark()` / `since=` pairs. Without them, a wait can match something from boot.

A panic, `abort()`, failed assert or stack overflow on the console fails the test, even one that
did not wait long enough to see it. `PANIC_PRINT_HALT` is set, so the backtrace is the last thing
in the log. Decode it with `xtensa-esp32-elf-addr2line -pfiaC -e .pio/build/qemu/firmware.elf <addrs>`.

**Which edges does a button send?** Whatever the HMI says: each component's `sendkey` (bit 0
release, bit 1 press; see `hmi_tool.py show FILE PAGE --attrs id,sendkey` in NSPanelManager), plus
any `printh 65 ...` its page's code sends for it. The scenes button reports release only, the
ceiling and table hotspots both, next-room and next-mode release only. `press()` sends exactly
that.

**The TFT's timers run in real time,** on the host's clock, not QEMU's. The one that matters is
`sleep_display`: after the firmware's screensaver timeout (FakeManager: 60 s) with no touch, the
page prints `0x92` and the panel goes to its screensaver, as a real one would.

## Stack tests

`tests/test_stack.py` runs the firmware against the real NSPanelManager instead of
`FakeManager`, using the dev stack from the NSPanelManager repo (`docker/dev`). Start the
stack, then give the harness its broker:

```
docker/dev/nspm-dev up                                          # in NSPanelManager; prints the broker port
NSPM_STACK_MQTT=127.0.0.1:<port> qemu/run_tests.sh -m stack     # here
```

`NSPM_STACK_WEB` is the stack's web UI (default `http://127.0.0.1:8010`). Tests marked
`stack` are skipped without `NSPM_STACK_MQTT`.

```
   QEMU panel ── slirp ──► FaultProxy ──► stack mosquitto ◄──► MQTTManager ◄── Django
                                               ▲                    ▲ STOMP     ▲ REST
                                               └ observer ─── StackManager ─────┘
```

Each panel gets a random MAC and its own hostname, so it is new to the stack. It registers,
shows up as awaiting accept, and `StackManager.accept()` accepts it through the same STOMP
topics (`entity_states`, `nspanel/<mac>/status`) and REST call
(`POST /rest/nspanel/<id>/accept`) the web UI uses. That makes any number of panels
possible: `make_rig()` builds another one. At teardown the panel is deleted from the stack
(`NSPM_STACK_KEEP_PANELS=1` keeps it, to look at it in the UI).

The stack is shared state, so `subscriptions()` and the connect/subscribe waits, which read
the harness's own mosquitto log, are unavailable here.

CI runs these tests in `.github/workflows/stack-tests.yml`: it checks out NSPanelManager,
starts its dev stack, waits for MQTTManager to build, and runs `-m stack`. The NSPanelManager
repository and branch are set at the top of that workflow (and can be overridden when it is
run by hand).

## What differs from hardware

The `qemu` PlatformIO env is `original_sonoff` with `-DNSPM_QEMU` and
`sdkconfig.qemu.defaults` overlaid on `sdkconfig.original_sonoff`. `sdkconfig.qemu` is generated
(`gen_sdkconfig.py`). The overlay enables OpenCores Ethernet, sets 4 MB flash and halts on panic.
It also enlarges the Ethernet RX ring: ESP-IDF's OpenCores ISR logs dropped frames with format
strings in flash, which panics with a cache error if the ring overflows during a flash write.
Each `NSPM_QEMU` block in the source exists because QEMU does not emulate something:

| Where | Hardware | Under QEMU | Why |
|---|---|---|---|
| `WiFiManager::init` | `esp_wifi_init()`, Wi-Fi STA and AP netifs | Only `esp_netif_init()` and the MAC | No Wi-Fi radio in QEMU |
| `WiFiManager::start_client` | Wi-Fi STA, fallback AP after a timeout | OpenCores Ethernet + DHCP, reported through the same `connected()` / `ip_info()`; no fallback AP | No Wi-Fi radio in QEMU |
| `Nextion::_task_uart_event` | UART pattern-detect interrupt splits frames on `FF FF FF` | Software framer on `UART_DATA` (`_qemu_frame_uart_data`) | QEMU's UART has no AT_CMD_CHAR detection, so the display's replies would be dropped |
| `Nextion::init` | Enables pattern detection | Skipped | Not emulated. Also, QEMU's DPORT model doesn't reflect the UART clock-enable bits, so under IDF 6 `uart_get_baudrate()` fails and the setup divides by zero |
| `StatusUpdateManager::init` / `_measure_temperature` | NTC via ADC1 (`adc_oneshot` + calibration) | No ADC setup; fixed 1680 mV (≈22 °C) | The ADC is unimplemented; a conversion spins forever with a spinlock held and freezes core 0 |
| `StatusUpdateManager::_send_status_update` | `esp_wifi_sta_get_rssi()` | −50 | Wi-Fi is never initialised, and the call dereferences driver state |

Consequences:

- **Wi-Fi reconnect logic is not exercised.** `WIFI_EVENT_STA_DISCONNECTED` is never posted, so
  the event-queue starvation hazards in CLAUDE.md involving Wi-Fi cannot be reproduced here.
  MQTT-level faults can (`broker.drop_client`).
- **The Nextion receive path is not the production one.** Frame splitting, and the order and
  timing of `UART_DATA` versus `UART_PATTERN_DET` events, differ. Bugs in that handling are
  out of reach.
- **Timing is not real.** QEMU runs the guest slower than real time and not cycle-accurately.
  Races may appear or vanish compared with hardware.
- **Buttons, relays and the buzzer** are GPIO/LEDC writes into models no test reads yet.

## Debugging

- `NSPM_QEMU_GDB=1` exposes a gdbstub on `:3333`. `NSPM_QEMU_GDB=wait` also halts at reset
  until GDB attaches:
  `~/.platformio/packages/tool-xtensa-esp-elf-gdb/bin/xtensa-esp32-elf-gdb .pio/build/qemu/firmware.elf -ex "target remote :3333"`.
  `thread apply all bt` shows both CPUs, which is how the ADC hang was found.
- `NSPM_QEMU_SKIP_BUILD=1 qemu/run_tests.sh` reruns against the existing build.
- QEMU comes from `install_qemu.sh`, pinned by release and SHA-256, into `~/.cache/nspm-qemu`.
  Set `QEMU_XTENSA` to use another binary.

## Protobufs

The firmware repo carries only protobuf-c output (`lib/ProtoBuf`), so `nspm_qemu/proto.py`
fetches the `.proto` sources from NSPanelManager, which owns the schema. It downloads
`docker/protobuf/*.proto` at a pinned commit (`PROTO_REF`), caches them under `build/proto/`,
and compiles them with grpcio-tools.

- `NSPM_PROTO_REF=<commit or branch>` uses another revision. Bump `PROTO_REF` to move the
  default.
- `NSPM_PROTO_DIR=~/nspanel/NSPanelManager/docker/protobuf` uses a local checkout, to test a
  schema change before it is pushed.

The manager's schema can be newer than the one `lib/ProtoBuf` was generated from. That is
wire-compatible: the firmware ignores fields it does not know.
