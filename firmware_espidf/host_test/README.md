# Host (x86) test harness — proof of concept

Runs firmware logic as an ordinary Linux binary. No ESP32, no QEMU, no ESP-IDF build.

```
make              # build and run the tests
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

Nothing under `lib/`, `src/` or `include/` is modified, and none of this is in the
PlatformIO build, so the firmware binary is unaffected.

## Status

18 of 19 firmware translation units compile on the host unchanged. `WiFiManager.cpp`
does not: it uses the esp_netif/DHCP/captive-portal API directly and needs the real
`espp::DnsServer`. It is also the least valuable unit to test in isolation.
