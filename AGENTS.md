# AGENTS.md

## Project Overview

Threaded ESPHome external component (`nuki_pro`) for Nuki 5.0 Pro smart lock on PoE-powered ESP32-S3 hardware. BLE operations run on a dedicated FreeRTOS task (Core 0) while ESPHome's main loop stays on Core 1. Event-driven loop gating keeps Core 1 idle until Core 0 signals new data.

## Cursor Cloud specific instructions

### Environment

All commands must run inside the venv: `source /workspace/.venv/bin/activate` (or prefix with `/workspace/.venv/bin/`). ESPHome (dev branch), PlatformIO, ruff, and clang-format are pre-installed by the update script. The setup script now warms `esphome config` + `esphome compile` cache data for `nuki-lock-test.yaml`, so incremental builds are fast (~5s) for new agents.

### Build

```bash
esphome compile nuki-lock-test.yaml
```

If setup cache warming is skipped (`ESPHOME_WARM_COMPILE=0`), first compile downloads the ESP32-S3 toolchain (~60s). Incremental builds take ~5s.

### Validate YAML

```bash
esphome config nuki-lock-test.yaml
```

### Lint

```bash
ruff check components/nuki_pro/*.py
clang-format --dry-run --Werror components/nuki_pro/*.cpp components/nuki_pro/*.h
```

### Key files

- `components/nuki_pro/lock.py` — ESPHome component registration, PIN validation, IDF component deps
- `components/nuki_pro/nuki_pro.h` — FreeRTOS primitives, atomic state, class definition
- `components/nuki_pro/nuki_pro.cpp` — BLE task with `xQueueReceive` hybrid engine
- `nuki-lock-test.yaml` — Test config for ESP32-S3 with W5500 SPI Ethernet

### Architecture notes

- ESPHome main loop runs on **Core 1** (`esphome/components/esp32/core.cpp` line 72)
- `nukiBLE` task pinned to **Core 0** via `xTaskCreatePinnedToCore`
- `xQueueReceive` timeout = poll interval. Commands wake instantly; timeouts trigger status polls.
- State exchange uses `std::atomic` + `enable_loop_soon_any_context()` — never call `publish_state()` from the BLE task
- `loop()` calls `disable_loop()` after each iteration; Core 0 wakes it via `enable_loop_soon_any_context()`
- NukiBleEsp32 and NimBLE libraries are fetched at build time via `add_idf_component()` in `lock.py`
- Component is incompatible with ESPHome's built-in BLE stack (`esp32_ble`, `esp32_improv`, etc.)
- PIN is a 6-digit string in YAML, parsed to `uint32_t` in Python codegen — zero runtime string parsing
- `nuki_id` configurable in YAML (default 2020002) for multi-lock deployments
- Pairing persistence uses `make_entity_preference<bool>(1)` — versioned, collision-free
- LwIP pinned to Core 1 (`CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU1=y`) to isolate BLE from network jitter
- 8-second keepalive pulse keeps BLE session warm without flooding the radio
