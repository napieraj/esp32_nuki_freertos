# Nuki 5.0 Pro — Threaded ESPHome Component

Ultra-responsive ESPHome external component for the Nuki 5.0 Pro smart lock. Runs the BLE stack on a dedicated FreeRTOS task so the ESPHome main loop never blocks.

## Architecture

```
Core 1 (ESPHome)          Core 0 (BLE task)
─────────────────         ──────────────────
loop() ←── atomics ────── xQueueReceive hybrid engine
control() ──→ queue ────→   ├─ command? → execute instantly
                             └─ timeout?  → aggressive poll
```

| Core | Task | What it does |
|------|------|--------------|
| Core 1 | ESPHome `loopTask` | WiFi, API, HA, `publish_state()` via atomic check |
| Core 0 | `nukiBLE` | BLE scanner, Nuki connection, lock commands, status polling |

### Key design

- **`xQueueReceive` hybrid engine**: The queue timeout IS the poll interval (100ms default). Commands from HA arrive with 0ms latency. Timeouts trigger status polls.
- **Lock-free state exchange**: `std::atomic` for pending state — no mutex, no priority inversion.
- **PIN as string**: 6-digit PIN preserved with leading zeros (`"065432"` stays `065432`).
- **Keepalive behavior**: Commands wake instantly via queue events; when idle, an 8-second keepalive pulse prevents Nuki BLE session timeout.

## Usage

```yaml
external_components:
  - source:
      type: local
      path: components

lock:
  - platform: nuki_pro
    name: "Front Door Nuki 5.0"
    pin: "065432"
    poll_interval: 100ms  # default
```

## UART bridge mode (`nuki_uart_bridge`)

An alternative lock platform for setups where the BLE side lives on a
dedicated **XIAO nRF52840** running the
[`nRF52840_nuki_bridge`](https://github.com/napieraj/nRF52840_nuki_bridge)
firmware.  The ESP32 is then only the *host* of that bridge's UART
protocol: no NimBLE, no NaCl, no pairing state on the ESP32 — everything
Nuki-specific runs on the nRF.  `nuki_pro` (direct BLE) and
`nuki_uart_bridge` are independent components; pick one per device.

```
HA ──Ethernet──► ESP32-S3 (this component) ──UART 921600 8N1──► nRF52840 ──BLE──► Nuki
```

### Wiring and UART

| ESP32 | nRF52840 XIAO |
|-------|---------------|
| `rx_pin` | D6 / P1.11 (bridge TX) |
| `tx_pin` | D7 / P1.12 (bridge RX) |
| GND | GND (3.3 V logic) |

The bridge runs **921600 8N1** by default; 115200 8N1 is the documented
fallback if the bridge overlay is built with that rate.  Both work here —
the two sides only have to agree, there is no autobaud.  Give the ESPHome
`uart` a `rx_buffer_size` of at least 512 (1024 in the example) so a burst
of a state change plus a diagnostics reply between two `loop()` runs cannot
overrun the driver ring.

### Configuration

```yaml
uart:
  id: nuki_uart
  tx_pin: GPIO17
  rx_pin: GPIO18
  baud_rate: 921600      # or 115200 to match the bridge overlay
  rx_buffer_size: 1024

lock:
  - platform: nuki_uart_bridge
    id: front_door
    name: "Front Door Nuki"
    uart_id: nuki_uart
    pin: "065432"        # optional 6-digit security PIN (mandatory to pair an Ultra/5th gen)
    device_type: auto    # auto | classic | ultra  (PAIR payload device_type)
    pair_as: app         # app | bridge            (Authorization Data ID type)
    link_profile: armed  # armed (7.5 ms interval, latency 0) | eco
    app_id: 2020002      # optional fixed App/Bridge-ID; omitted = bridge picks one
    poll_interval: 60s   # periodic REQ_LOCK_STATE; state normally arrives unsolicited
    auto_pair: true      # send PAIR once at boot if the bridge reports no credentials
    connected:           # optional binary_sensor: bridge BLE state == CONNECTED
      name: "Front Door Nuki Connected"
    rssi:                # optional sensor (dBm), from CONN_STATUS events
      name: "Front Door Nuki RSSI"
    diagnostics:         # optional text_sensor summarising REQ_DIAGNOSTICS
      name: "Front Door Nuki Bridge Diagnostics"
    on_pairing_complete: # x = auth_id (uint32)
      - logger.log: "paired"
    on_state_change:     # x = raw Nuki lock state byte (spec p.31)
      - logger.log: "state changed"
```

Lambda-callable helpers: `id(front_door).pair()`, `.unpair()` (sends the
PIN if configured so the bridge can remove its authorization from the
lock), `.request_diagnostics()`, `.request_lock_state()`,
`.set_runtime_link_profile(0|1)`.  `lock.open` maps to UNLATCH.

See `nuki-uart-bridge-test.yaml` for a complete W5500 + template-button
example.  Validate with `make config-uart`, build with `make compile-uart`.

### Protocol implemented (host side)

* Framing: `0x00 COBS([TYPE][SEQ LE16][DATA][CRC16 LE]) 0x00`,
  CRC-16/CCITT-FALSE (0x1021 / 0xFFFF).  Pure, host-testable code lives in
  `components/nuki_uart_bridge/nuki_uart_framing.h`; `make test-host` runs
  `tests/test_uart_framing.c` against COBS/CRC vectors, the worked HELLO
  and UNLOCK bytes from the bridge's `docs/host-integration.md`, and
  round-trips.
* Startup: HELLO `[0x7D][0x02][CRC]` is always sent **v1-framed**
  (`00 05 7D 02 48 43 00` on the wire); the `0x90` reply selects v2 and
  every later frame carries a monotonic SEQ starting at 1 (never 0).  A
  `0x90` with `selected = 1` means the bridge rebooted: in-flight requests
  are dropped and HELLO is renegotiated.  HELLO is retried with backoff
  (1 → 8 s) and the link is declared lost after 90 s without any frame.
* Commands: UNLOCK 0x01, LOCK 0x02, UNLATCH 0x03, REQ_LOCK_STATE 0x10,
  PAIR 0x05 `[device_type][pin LE32][id_type][app_id LE32]` (trailing
  defaults omitted), UNPAIR 0x74 `[pin LE32]?`, SET_LINK_PROFILE 0x76,
  PING 0x7C every 30 s (bridge marks the host stale after 120 s),
  REQ_DIAGNOSTICS 0x7E.
* Responses: ACK 0x80, ERROR 0x82 (error byte named in the log; a failed
  action publishes `NONE` and re-requests the state), STATUS 0x81
  (`[0E 00 01]` = Nuki *Status ACCEPTED* for the action's SEQ, or a raw
  forwarded lock frame), STATE_CHANGE 0x85 (`[0C 00][Keyturner States]`,
  length-driven parse: byte 1 = lock state → ESPHome state, `0xFE` motor
  blocked → `JAMMED`), ERROR_REPORT 0x8E (Nuki error named in the log),
  CONN_STATUS 0x8D (state / RSSI / host-link / beacon), PAIRING_COMPLETE
  0x83, DIAGNOSTICS 0x8C v0x03 and v0x04 (device type, MTU, counters,
  armed engine state, connection interval and the three armed-path latency
  stages are logged; a summary goes to the `diagnostics` text sensor).
* Latency: `lock`/`unlock`/`open` write the frame **immediately from
  `control()`** on the ESPHome main loop and publish the optimistic
  `LOCKING`/`UNLOCKING` state; the confirmed state comes from the
  unsolicited `0x85`.  The host logs the ms from UART TX to *Status
  ACCEPTED* and to the confirming Keyturner States.  `loop()` only drains
  the UART ring (bounded to 512 bytes per iteration) and runs timers — no
  blocking, no heap churn.

## Requirements

- ESP32-S3 (dual-core) with ESP-IDF framework
- PSRAM recommended
- Nuki 5.0 Pro / Ultra / 5th Gen smart lock

## Development

### Quick start

```bash
git clone https://github.com/napieraj/esp32_nuki_freertos.git
cd esp32_nuki_freertos
make setup      # creates .venv, installs ESPHome dev + tools
make config     # validate YAML
make compile    # cross-compile firmware for ESP32-S3
```

**Prerequisites:** Python 3.12+ and `clang-format` (optional, for C++ formatting).

`make setup` now pre-warms `esphome config` + `esphome compile` cache data so
subsequent `make compile` calls avoid first-time toolchain download delays.
Set `ESPHOME_WARM_COMPILE=0` to skip warm-up.

### Make targets

| Target | Description |
|--------|-------------|
| `make setup` | Create venv, install tools, and warm compile cache |
| `make setup-fast` | Setup only (skip warm compile cache) |
| `make config` | Validate ESPHome YAML config |
| `make compile` | Compile firmware for ESP32-S3 |
| `make lint` | Run all linters (ruff + clang-format) |
| `make config-uart` | Validate the UART-bridge YAML (`nuki-uart-bridge-test.yaml`) |
| `make compile-uart` | Compile the UART-bridge firmware |
| `make test-host` | Run the pure-C UART framing tests with the host gcc |
| `make format` | Auto-format all source files |
| `make clean` | Remove build artifacts (keeps venv) |
| `make clean-all` | Remove build artifacts, venv, and toolchain cache |

### VS Code / Codespaces

Open in a [dev container](https://containers.dev/) for a zero-config environment — `script/setup` runs automatically on creation.

### Manual setup

If you prefer not to use `make`:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install "esphome @ git+https://github.com/esphome/esphome.git@dev" ruff
esphome config nuki-lock-test.yaml   # validate
esphome compile nuki-lock-test.yaml  # build
```
