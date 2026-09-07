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
    time_id: sntp_time   # optional: Update Time 0x0021 daily and after every time sync
    pairing_mode_timeout: 300s  # how long the pairing_mode switch keeps retrying PAIR
    allowed_actions: [unlock, lock, unlatch, lock_n_go, lock_n_go_unlatch, full_lock, fob_1, fob_2, fob_3]
    action_suffix: ""    # Lock Action name suffix (<= 20 bytes) shown in the lock's log; "" = friendly name
    event: nuki_event    # fire `esphome.nuki_event` in HA for every new log entry; "none" = off
    event_log_count: 5   # newest entries fetched after each lock/unlock/door change (1-50)
    connected:           # binary_sensor: bridge BLE state == CONNECTED
      name: "Front Door Nuki Connected"
    paired:              # binary_sensor: bridge holds credentials (diagnostics creds_valid / STATUS)
      name: "Front Door Nuki Paired"
    rssi:                # sensor (dBm), from CONN_STATUS events
      name: "Front Door Nuki RSSI"
    diagnostics:         # text_sensor summarising REQ_DIAGNOSTICS
      name: "Front Door Nuki Bridge Diagnostics"
    pin_status:          # text_sensor: Not set | Validation pending | Valid | Invalid
      name: "Front Door Nuki PIN Status"
    nuki_state:          # text_sensor: uninitialized | pairingMode | doorMode | maintenanceMode
      name: "Front Door Nuki State"
    firmware_version:    # text_sensors from Config 0x0015 (read on connect and on config changes)
      name: "Front Door Nuki Firmware"
    hardware_version:
      name: "Front Door Nuki Hardware"
    lock_name:
      name: "Front Door Nuki Name"
    nuki_id:             # hex, as the Nuki Bridge / Web API present it
      name: "Front Door Nuki ID"
    keypad_paired:       # binary_sensor: Config has_keypad || has_keypad2
      name: "Front Door Nuki Keypad Paired"
    battery_level:       # sensor (%): Keyturner States byte 12 bits 2-7 (steps of 2)
      name: "Front Door Nuki Battery"
    battery_critical:    # binary_sensor: byte 12 bit 0
      name: "Front Door Nuki Battery Critical"
    battery_charging:    # binary_sensor: byte 12 bit 1
      name: "Front Door Nuki Battery Charging"
    keypad_battery_critical:       # binary_sensor: accessory byte 20 bit 1 (unavailable when no keypad, bit 0)
      name: "Front Door Nuki Keypad Battery Critical"
    door_sensor_battery_critical:  # binary_sensor: byte 20 bit 3 (unavailable without a door sensor, bit 2)
      name: "Front Door Sensor Battery Critical"
    last_unlock_user:    # text_sensor: authorization name of the newest lock/keypad log entry
      name: "Front Door Nuki Last Unlock User"
    last_lock_action:    # text_sensor: Keyturner States "last lock action" (Unlock, Lock, ...)
      name: "Front Door Nuki Last Lock Action"
    last_lock_action_trigger:  # text_sensor: its trigger (system, manual, button, autoLock, ...)
      name: "Front Door Nuki Last Lock Action Trigger"
    last_lock_action_completion_status:  # text_sensor: success, motorBlocked, busy, invalidCode, ...
      name: "Front Door Nuki Last Lock Action Completion"
    night_mode:          # binary_sensor: Keyturner States byte 19
      name: "Front Door Nuki Night Mode"
    door_sensor:         # binary_sensor (door): 0x03 opened = ON, 0x02 closed = OFF, else unavailable
      name: "Front Door"
    door_sensor_state:   # text_sensor with the spec names (doorClosed, doorOpened, uncalibrated, tampered, ...)
      name: "Front Door Sensor State"
    door_security_state: # text_sensor: closedAndLocked | closedAndUnlocked | open | unknown (as hass_nuki_ng)
      name: "Front Door Security State"
    tamper:              # binary_sensor (tamper): ON while the door sensor reports 0xF0 Tampered
      name: "Front Door Sensor Tamper"
    pairing_mode:        # switch: PAIR now, retry every 5 s, off after pairing_mode_timeout / PAIRING_COMPLETE
      name: "Front Door Nuki Pairing Mode"
    unpair:              # button: UNPAIR 0x74 (with the PIN when configured)
      name: "Front Door Nuki Unpair"
    request_calibration: # button: Request Calibration 0x001A (PIN)
      name: "Front Door Nuki Calibrate"
    reboot:              # button: Request Reboot 0x001D (PIN)
      name: "Front Door Nuki Reboot"
    on_pairing_complete: # x = auth_id (uint32)
      - logger.log: "paired"
    on_pairing_mode_on:
      - logger.log: "pairing mode on"
    on_pairing_mode_off:
      - logger.log: "pairing mode off"
    on_state_change:     # x = raw Nuki lock state byte (spec p.31)
      - logger.log: "state changed"
    on_event_log:        # entry = nuki_log_entry_t (index, ts, auth_id, name, type, data[])
      - logger.log:
          format: "log #%u type 0x%02X by %s"
          args: ["entry.index", "entry.type", "entry.name"]
    on_door_state:       # door_state = raw door sensor byte (spec p.32)
      - logger.log: "door changed"
```

Automation actions: `nuki_uart_bridge.pair`, `.unpair`,
`.set_pairing_mode: {pairing_mode: true}`, `.set_security_pin:
{security_pin: 123456}` (runtime override, persisted in preferences, `0`
clears it — the fork's `nuki_lock.set_security_pin`), `.verify_pin`,
`.update_time`, `.request_calibration`, `.reboot`, `.lock_n_go: {unlatch:
false, suffix: "Alice"}` and `.lock_action: {action: fob_1, suffix: ...}`
(`action` is one of the `allowed_actions` names).  Conditions:
`nuki_uart_bridge.paired`, `nuki_uart_bridge.connected`.

Lambda-callable helpers: `id(front_door).pair()`, `.unpair()` (sends the
PIN if configured so the bridge can remove its authorization from the
lock), `.set_pairing_mode(bool)`, `.request_diagnostics()`,
`.request_lock_state()`, `.request_config()`,
`.set_runtime_link_profile(0|1)`, `.set_runtime_state_poll(seconds)`,
`.request_event_logs(n)`, `.print_keypad_entries()`,
`.add_keypad_entry(name, code)`, `.update_keypad_entry(id, name, code,
enabled)`, `.delete_keypad_entry(id)`, `.lock_n_go(unlatch)`,
`.full_lock()`, `.fob_action(1..3)`, `.lock_action(cmd, suffix)`,
`.set_security_pin(pin)`, `.verify_pin()`, `.update_time()`,
`.request_calibration()`, `.request_reboot()`,
`.set_runtime_action_suffix(name)`, `.pair_host()` / `.unpair_host()`
(secure link).  `lock.open` maps to UNLATCH.

`pair_as: bridge` **evicts the official Nuki Bridge**: a lock holds exactly
one Bridge-type authorization, so pairing as Bridge unregisters the Nuki
Bridge (ESPHome_nuki_lock README, nuki_hub README) — keep the default
`app` if a Nuki Bridge stays in use.  The flip side (nuki_hub `HYBRID.md`):
the lock only raises the "state changed" beacon bit for a Bridge-type
authorization, so a host paired as App relies on `state_poll_interval`
for manual turns; Ultra / Go / 5.0 Pro accept no Bridge registration at
all.

### Actions, retries and the guard window

* `allowed_actions` is an ACL checked before anything is sent (nuki_hub's
  per-action ACL); a refused action re-publishes the current state so HA
  does not stay optimistic.
* For **6 s after the bridge link comes up and after every Home Assistant
  API (re)connect** lock calls are ignored (nuki_hub's post-connect
  replay guard: a reconnecting HA can flush stale service calls).
* An action that never reached the lock — `ERROR 0x22 NOT_CONNECTED`, or
  `0x20 TIMEOUT` without a *Status ACCEPTED* — is re-sent **once** after
  the next `CONNECTED` inside a 10 s window.  Anything the lock may have
  acted on is never re-sent (pyNukiBT sends a Lock Action exactly once;
  a repeat would double-actuate or hit `K_ERROR_BAD_NONCE`).
* Transition watchdog: *ACCEPTED* but no settled `0x85` within 5 s →
  `REQ_LOCK_STATE` (the state push can be lost across a reconnect).
* Nuki state (Keyturner States byte 0) other than *door mode* → the entity
  reports unknown, like the core `nuki` integration's `ERROR_STATES`.
* Every action carries the **name suffix** (`SET_ACTION_SUFFIX 0x15`,
  default = the ESPHome friendly name; `set_action_suffix` service to
  change it at runtime, e.g. from an HA automation that knows the calling
  user), so the lock's own log names the actor.  A per-call `suffix` on
  `lock_n_go` / `lock_action` is sent as the action's payload instead.
  ESPHome's native API gives a custom service no user context, so the HA
  side has to pass the name explicitly.

### Security PIN lifecycle

`pin_status` follows the BLE component: *Not set* → *Validation pending*
(PIN known) → *Valid* / *Invalid*.  After boot, after every pairing and
after `set_security_pin` the host sends `VERIFY_PIN 0x14` (Verify Security
PIN 0x0020, `[PIN]` = `14 <seq> 98 FF 00 00` for Ultra PIN 065432) as soon
as the bridge is connected and the PIN width is known; *Status COMPLETE*
marks it valid (persisted), `K_ERROR_BAD_PIN` / `K_ERROR_TOO_MANY_PIN_
ATTEMPTS` from **any** PIN command mark it invalid and block every
further PIN command (keypad, log, calibration, time) until
`set_security_pin` / `verify_pin` succeed — the lock locks the PIN out
after repeated failures (spec p.76).  A bridge without `0x14`
(`UNKNOWN_CMD`) leaves the state at *Validation pending* and does not
block anything.  `security_pin` accepts 1-6 digits (leading zeros are
kept in YAML, the PIN is a number: `"000548"` = 548); `device_type:
classic` rejects values above 65535 at config time.

### Lock time

With `time_id`, `UPDATE_TIME 0x13` = Update Time 0x0021 is sent 30 s after
every time sync and once a day: `[year LE16][month][day][hour][min][sec]
+ PIN`, e.g. `13 09 00 EA 07 09 07 0C 22 38 98 FF 00 00` for 2026-09-07
12:34:56 on an Ultra, SEQ 9.  The wall clock of the configured timezone
is sent — the lock keeps local time and reports its timezone offset
separately (Keyturner States, Config), which is what nuki_hub does;
pyNukiBT sends UTC instead, so if the lock's log timestamps come out
shifted by the UTC offset on your firmware, set `timezone: UTC` on the
`time:` component.  Years before 2025 are refused (NTP not synced yet).

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
| `lock_n_go` | `unlatch` (bool) | `0x06 LOCK_N_GO` / `0x08 LOCK_N_GO_UNLATCH` (Lock Action 0x04 / 0x05), the `nuki.lock_n_go` semantics of HA core |
| `full_lock` | – | `0x09 FULL_LOCK` (Lock Action 0x06) |
| `fob_action` | `n` (1-3) | `0x0A`-`0x0C FOB_1..3` (Lock Action 0x81-0x83) |
| `update_time` | – | `0x13 UPDATE_TIME` = Update Time 0x0021 `[time:7]` + PIN (needs `time_id`) |
| `verify_pin` | – | `0x14 VERIFY_PIN` = Verify Security PIN 0x0020, PIN only |
| `set_action_suffix` | `name` (<= 20 bytes) | `0x15 SET_ACTION_SUFFIX [name]`, ACK only; used by every following action |
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

Config: `REQ_CONFIG 0x20` (Request Config 0x0014, nK only) is sent after
every connect and again whenever the Keyturner States *config update
count* (byte 13) changes (pyNukiBT / RaspiNukiBridge use it as a cache
invalidation counter); the `0x86` reply is parsed length-driven (72-byte
base through HomeKit status, then timezone id, device type, capabilities,
keypad 2.0 and Matter status when present) and feeds `firmware_version`,
`hardware_version`, `lock_name`, `nuki_id`, `keypad_paired`.  The Config
*device type* (0x05 Ultra) is the authoritative generation for the PIN
width once read, ahead of the bridge's advertisement-based classification.

Door sensor: the Keyturner States byte 18 feeds `door_sensor`,
`door_sensor_state`, `tamper`, `door_security_state` and `on_door_state`.  The 1.0-2.0 and
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
* Commands: UNLOCK 0x01, LOCK 0x02, UNLATCH 0x03, LOCK_N_GO 0x06,
  LOCK_N_GO_UNLATCH 0x08, FULL_LOCK 0x09, FOB_1..3 0x0A-0x0C (each with
  an optional name-suffix payload), REQ_LOCK_STATE 0x10, UPDATE_TIME 0x13,
  VERIFY_PIN 0x14, SET_ACTION_SUFFIX 0x15, REQ_CONFIG 0x20, PAIR 0x05
  `[device_type][pin LE32][id_type][app_id LE32]` (trailing defaults
  omitted), REQ_CALIBRATION 0x70 / REQ_REBOOT 0x73 (PIN only), UNPAIR
  0x74 `[pin LE32]?`, SET_LINK_PROFILE 0x76, PING 0x7C every 30 s (bridge
  marks the host stale after 120 s), REQ_DIAGNOSTICS 0x7E.
* Responses: ACK 0x80, ERROR 0x82 (error byte named in the log; a failed
  action publishes `NONE` and re-requests the state), STATUS 0x81
  (`[0E 00 01]` = Nuki *Status ACCEPTED* for the action's SEQ, or a raw
  forwarded lock frame), STATE_CHANGE 0x85 (`[0C 00][Keyturner States]`,
  length-driven parse: byte 1 = lock state → ESPHome state, `0xFE` motor
  blocked → `JAMMED`), ERROR_REPORT 0x8E (Nuki error named in the log),
  CONN_STATUS 0x8D (state / RSSI / host-link / beacon), PAIRING_COMPLETE
  0x83, CONFIG 0x86 (`[15 00][Config]`), DIAGNOSTICS 0x8C v0x03, v0x04
  and v0x05 (device type, MTU, counters, armed engine state, connection
  interval, the three armed-path latency stages, secure-link state and PIN
  flags are logged; a summary goes to the `diagnostics` text sensor).
* Keyturner States (`nuki_uart_parse_keyturner()`): nuki state, lock
  state, trigger, battery (level / critical / charging), config update
  count, last action / trigger / completion status, door sensor, night
  mode and accessory battery bits, each gated on the payload length (the
  2016 example on spec p.87 is 13 bytes, current firmware sends 22+).
* Latency: `lock`/`unlock`/`open` write the frame **immediately from
  `control()`** on the ESPHome main loop and publish the optimistic
  `LOCKING`/`UNLOCKING` state; the confirmed state comes from the
  unsolicited `0x85`.  The host logs the ms from UART TX to *Status
  ACCEPTED* and to the confirming Keyturner States.  `loop()` only drains
  the UART ring (bounded to 512 bytes per iteration) and runs timers — no
  blocking, no heap churn.

### Latency compared with the other Home Assistant routes

| Route | HA call → motor starts | Manual turn → HA | Why |
|-------|------------------------|------------------|-----|
| ESPHome_nuki_lock (NukiBleEsp32 on the ESP32) | ≥ 1.5-3 s | beacon bit → next 500 ms tick + connect | 500 ms poll tick, connect 2 s × 5 retries per command, challenge + action, 3 s cooldown after a lock action (`nuki_lock.h:46-57`) |
| nuki_hub (MQTT) | ≈ 1-2 s | beacon → state read | same library; connect per command, 3 s command timeout, up to 4 retries |
| Nuki Bridge HTTP API (HA core `nuki`, hass_nuki_ng) | 1-3 s | 30 s poll or bridge callback | `/lockAction` returns before the motor runs; state by polling / webhook |
| **this component, armed link** | **< 60 ms** UART byte → *Status ACCEPTED* | one connection interval + `state_poll_interval` for the door sensor | persistent 7.5 ms link, pre-built encrypted frame, single GATT write (`docs/latency.md` on the bridge) |

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
| `make test-host` | Run the pure-C UART framing + entry/Config/Keyturner parser tests with the host gcc |
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
