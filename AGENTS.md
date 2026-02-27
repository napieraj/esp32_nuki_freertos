# AGENTS.md

## Project Overview

Threaded ESPHome external component (`nuki_pro`) for Nuki 5.0 Pro smart lock. BLE operations run on a dedicated FreeRTOS task (Core 0) while ESPHome's main loop stays on Core 1.

## Cursor Cloud specific instructions

### Build

```bash
esphome compile nuki-lock-test.yaml
```

First build downloads the ESP32-S3 toolchain (~60s). Incremental builds take ~5s.

### Validate YAML

```bash
esphome config nuki-lock-test.yaml
```

### Key files

- `custom_components/nuki_pro/lock.py` — ESPHome component registration, PIN validation, IDF component deps
- `custom_components/nuki_pro/nuki_pro.h` — FreeRTOS primitives, atomic state, class definition
- `custom_components/nuki_pro/nuki_pro.cpp` — BLE task with `xQueueReceive` hybrid engine
- `nuki-lock-test.yaml` — Test config for ESP32-S3

### Architecture notes

- ESPHome main loop runs on **Core 1** (`esphome/components/esp32/core.cpp` line 72)
- `nukiBLE` task pinned to **Core 0** via `xTaskCreatePinnedToCore`
- `xQueueReceive` timeout = poll interval. Commands wake instantly; timeouts trigger status polls.
- State exchange uses `std::atomic` — never call `publish_state()` from the BLE task
- NukiBleEsp32 and NimBLE libraries are fetched at build time via `add_idf_component()` in `lock.py`
- Component is incompatible with ESPHome's built-in BLE stack (`esp32_ble`, `esp32_improv`, etc.)
- PIN is a 6-digit string to preserve leading zeros for Nuki 5.0 Pro
