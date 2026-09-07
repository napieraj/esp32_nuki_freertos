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
    security_pin: "065432"  # 4-6 digits, leading zeros kept; needed to pair an Ultra and
                            # for every keypad / event-log command (`pin:` is the old name)
    device_type: auto    # auto | classic | ultra  (PAIR payload device_type)
    pair_as: app         # app | bridge            (Authorization Data ID type)
    link_profile: armed  # armed (7.5 ms interval, latency 0) | eco
    app_id: 2020002      # optional fixed App/Bridge-ID; omitted = bridge picks one
    poll_interval: 60s   # periodic REQ_LOCK_STATE; state normally arrives unsolicited
    auto_pair: true      # send PAIR once at boot if the bridge reports no credentials
    secure_link: auto    # auto | true | false — sealed UART transport (see below)
    state_poll_interval: 60s  # bridge-side Keyturner States read cadence (SET_STATE_POLL 0x7A)
    event: nuki_event    # fire `esphome.nuki_event` in HA for every new log entry; "none" = off
    event_log_count: 5   # newest entries fetched after each lock/unlock/door change (1-50)
    connected:           # optional binary_sensor: bridge BLE state == CONNECTED
      name: "Front Door Nuki Connected"
    rssi:                # optional sensor (dBm), from CONN_STATUS events
      name: "Front Door Nuki RSSI"
    diagnostics:         # optional text_sensor summarising REQ_DIAGNOSTICS
      name: "Front Door Nuki Bridge Diagnostics"
    last_unlock_user:    # text_sensor: authorization name of the newest lock/keypad log entry
      name: "Front Door Nuki Last Unlock User"
    last_lock_action:    # text_sensor: Keyturner States "last lock action" (Unlock, Lock, ...)
      name: "Front Door Nuki Last Lock Action"
    last_lock_action_trigger:  # text_sensor: its trigger (system, manual, button, autoLock, ...)
      name: "Front Door Nuki Last Lock Action Trigger"
    door_sensor:         # binary_sensor (door): 0x03 opened = ON, 0x02 closed = OFF, else unavailable
      name: "Front Door"
    door_sensor_state:   # text_sensor with the spec names (doorClosed, doorOpened, uncalibrated, tampered, ...)
      name: "Front Door Sensor State"
    tamper:              # binary_sensor (tamper): ON while the door sensor reports 0xF0 Tampered
      name: "Front Door Sensor Tamper"
    on_pairing_complete: # x = auth_id (uint32)
      - logger.log: "paired"
    on_state_change:     # x = raw Nuki lock state byte (spec p.31)
      - logger.log: "state changed"
    on_event_log:        # entry = nuki_log_entry_t (index, ts, auth_id, name, type, data[])
      - logger.log:
          format: "log #%u type 0x%02X by %s"
          args: ["entry.index", "entry.type", "entry.name"]
    on_door_state:       # door_state = raw door sensor byte (spec p.32)
      - logger.log: "door changed"
```

Lambda-callable helpers: `id(front_door).pair()`, `.unpair()` (sends the
PIN if configured so the bridge can remove its authorization from the
lock), `.request_diagnostics()`, `.request_lock_state()`,
`.set_runtime_link_profile(0|1)`, `.set_runtime_state_poll(seconds)`,
`.request_event_logs(n)`, `.print_keypad_entries()`,
`.add_keypad_entry(name, code)`, `.update_keypad_entry(id, name, code,
enabled)`, `.delete_keypad_entry(id)`, `.pair_host()` / `.unpair_host()`
(secure link).  `lock.open` maps to UNLATCH.

### Keypad, event log and door sensor

These need `security_pin` (the lock demands the PIN for every keypad and
log command) and, for the Home Assistant services, `api: custom_services:
true`; events additionally need `api: homeassistant_services: true`.

| HA service (`esphome.<node>_…`) | Arguments | UART command sent |
|---------------------------------|-----------|-------------------|
| `add_keypad_entry` | `name` (1-20 chars), `code` (6 digits, no 0) | `0x50 ADD_KEYPAD` = Add Keypad Code 0x0041 fields (code, name, time-limited block zeroed) + PIN |
| `update_keypad_entry` | `id`, `name`, `code`, `enabled` | `0x53 UPDATE_KEYPAD` = Update Keypad Code 0x0046 + PIN |
| `delete_keypad_entry` | `id` | `0x54 REMOVE_KEYPAD` = Remove Keypad Code 0x0047 + PIN |
| `print_keypad_entries` | – | `0x51 REQ_KEYPAD_CODES` (offset 0, count 0xFFFF) + PIN; every `0x89` entry is logged (id, name, enabled, lock count, dates — never the code) |
| `request_event_logs` | `count` (1-50) | `0x40 REQ_LOG_ENTRIES` (start 0, descending, no count frame) + PIN; every `0x88` entry is parsed |
| `pair_host` | – | secure-link bootstrap, see below |

Payloads follow the bridge rule "spec fields minus nK, PIN last": the PIN
container is `uint16` on gen 1-4 and `uint32` on Ultra (spec p.16) and the
width is picked when the command is sent from the `device_type` the
bridge reports in diagnostics (falling back to the configured one).  A
4-6 digit PIN string is accepted; leading zeros are kept in YAML and
encoded numerically.  Example, `add_keypad_entry("Guest", 123456)` on an
Ultra with PIN 065432, SEQ 7:

```
payload  40 E2 01 00 | "Guest" + 15 x 00 | 00 | 19 x 00 | 98 FF 00 00
         code 123456 LE   name[20]         tl   limits    PIN 65432 LE32
frame    50 07 00 <payload> CRC   →  wire 00 03 50 07 04 40 E2 01 06 47 75 65 73 74 01 … 03 98 FF 01 03 DE 21 00
```

Event log pipeline: after every settled lock state change and every door
sensor change the host waits 2 s and fetches the newest
`event_log_count` entries (also `request_event_logs(n)` on demand).  The
bridge streams them as `0x88` frames, one Log Entry 0x0032 each (index,
lock timestamp, auth id, name, type, data — length-driven, spec pp.50-52).
For every entry whose index is above the last one seen the component
fires `esphome.<event>` with the same keys as the NukiBleEsp32-based
ESPHome component (`index`, `authorizationId`, `authorizationName`,
`timeYear`…`timeSecond`, `type`, `action`, `trigger`, `completionStatus`,
`codeId`) plus an ISO `timestamp` taken from the lock's own clock — so
door-sensor entries (type 0x06: `DoorOpened` / `DoorClosed` /
`SensorJammed` / `SensorTampered`) and keypad entries are timed exactly
even though they are fetched by polling — and runs `on_event_log`.
Entries arrive newest first; on the first fetch after boot every entry of
the batch fires.  `last_unlock_user` is the name of the newest lock/keypad
entry, resolved through the authorization list (`0x30 REQ_AUTH_ENTRIES`
→ `0x87` stream, fetched once per connection and refreshed every 6 h),
then the name inside the entry, then `Manual`; a Keyturner States with
trigger `manual` also sets it to `Manual`.

Door sensor: the Keyturner States byte 18 feeds `door_sensor`,
`door_sensor_state`, `tamper` and `on_door_state`.  The 1.0-2.0 and
3.0-Ultra value sets do not overlap, so one table names both
(`unavailable`, `deactivated`, `doorClosed`, `doorOpened`,
`doorStateUnknown`, `calibrating`, `uncalibrated`, `tampered`, `unknown`).
The bridge emits `0x85` whenever the door sensor byte changes, but a
closed door only becomes visible when the bridge next reads the state:
`state_poll_interval` (sent as `SET_STATE_POLL 0x7A [seconds LE16]` after
HELLO, `0` = leave the bridge default) sets that cadence and is the
door-sensor freshness bound.

### Secure link (`secure_link`)

The bridge's Phase 3 firmware seals the UART (X25519 static + ephemeral
keys, BLAKE2b key schedule, XChaCha20-Poly1305, per-direction counters;
`components/nuki_uart_bridge/nuki_uart_seclink.*`, byte-exact against the
bridge's `tests/sec_link_vectors.txt` via `make test-seclink`).  The host
keeps its static key pair and the bridge's public key in ESPHome
preferences (keyed by the lock entity).

* `auto` (default): use it when the HELLO caps announce it (`0x0020`);
  older bridge firmware keeps working in plaintext.
* `true`: refuse to talk to a bridge without it.  `false`: never seal.
* First contact (bridge unpaired, caps without `0x0040`): the host sends
  `PAIR_WINDOW 0x77 [120]` and its `[E3][pk]`; the bridge answers with its
  key inside its pairing window (first boot, its button, or `0x77` while
  unpaired) — trust on first use.  A bridge that already holds another
  host's key is left alone: open its window and call `pair_host()`.
* Every boot / bridge restart: HS_INIT → HS_RESP (retried 3× with
  backoff); link profile, state poll, diagnostics and state requests are
  held until the session is up.  From then on every command is an inner
  `[cmd][seq][data]` sealed into `[E0][ctr][tag][ct]`, and every sealed
  reply is opened and dispatched through the normal SEQ/ACK logic.
  `0x82 [08]` re-runs the handshake; `0x82 [41]` stops and asks for a
  re-pair.  `unpair_host()` wipes the bridge's copy of the host key.
* Diagnostics v5 (`sec_state`, pairing window, `state_poll_interval_s`) is
  parsed by version and length.

See `nuki-uart-bridge-test.yaml` for a complete W5500 + template-button
example.  Validate with `make config-uart`, build with `make compile-uart`.

### Protocol implemented (host side)

* Framing: `0x00 COBS([TYPE][SEQ LE16][DATA][CRC16 LE]) 0x00`,
  CRC-16/CCITT-FALSE (0x1021 / 0xFFFF).  Pure, host-testable code lives in
  `components/nuki_uart_bridge/nuki_uart_framing.h`; `make test-host` runs
  `tests/test_uart_framing.c` against COBS/CRC vectors, the worked HELLO
  and UNLOCK bytes from the bridge's `docs/host-integration.md`, and
  round-trips, plus `tests/test_uart_entries.c` for the keypad / log /
  authorization builders and parsers in `nuki_uart_entries.h` (hand-built
  frames from the spec field tables — Nuki publishes no vectors for them).
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
| `make test-host` | Run the pure-C UART framing + entry parser tests with the host gcc |
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
