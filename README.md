# esp32_nuki_freertos

Threaded ESPHome Nuki Lock component for ESP32-S3. Forked from [ESPHome_nuki_lock](https://github.com/uriyacovy/ESPHome_nuki_lock).

## Architecture

The original component runs all Nuki BLE operations on the ESPHome main loop, blocking it during BLE connect/command/disconnect cycles. This fork moves all BLE operations to a dedicated FreeRTOS task:

| Core | Task | Responsibility |
|------|------|----------------|
| **Core 1** | ESPHome main loop | WiFi, API, HA, entity state publishing, UI |
| **Core 0** | `nukiBLE` task | BLE scanner, Nuki connection keep-alive, lock commands |

### Key improvements over the original

- **Non-blocking**: BLE operations never stall the ESPHome main loop
- **Keep-alive**: Persistent BLE connection with 10s polling interval for instant lock/unlock
- **Thread-safe IPC**: FreeRTOS queues for command dispatch (Core 1→0) and result delivery (Core 0→1)
- **All `publish_state()` calls on Core 1**: Safe ESPHome API usage

## Usage

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/YOUR_USER/esp32_nuki_freertos
    components: [nuki_lock]

lock:
  - platform: nuki_lock
    name: "Nuki Lock"
    security_pin: 123456
    # ... same config options as the original component
```

## Requirements

- ESP32-S3 (dual-core)
- ESP-IDF framework (not Arduino)
- PSRAM recommended
