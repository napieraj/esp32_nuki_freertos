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
- **Keep-alive polling**: Constant 100ms cycle keeps the BLE connection warm for instant actions.

## Usage

```yaml
external_components:
  - source:
      type: local
      path: custom_components

lock:
  - platform: nuki_pro
    name: "Front Door Nuki 5.0"
    pin: "065432"
    poll_interval: 100ms  # default
```

## Requirements

- ESP32-S3 (dual-core) with ESP-IDF framework
- PSRAM recommended
- Nuki 5.0 Pro / Ultra / 5th Gen smart lock
