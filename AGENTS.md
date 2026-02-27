# AGENTS.md

## Project Overview

Threaded ESPHome external component for Nuki smart lock BLE integration on ESP32-S3. Runs the Nuki BLE stack on a dedicated FreeRTOS task (Core 0) while ESPHome's main loop stays on Core 1.

## Cursor Cloud specific instructions

### Environment

- ESPHome is installed via pip: `pip install esphome`. Binary is at `~/.local/bin/esphome`.
- Ensure `~/.local/bin` is on PATH.
- PlatformIO is managed by ESPHome automatically (installed into `~/.platformio/`).
- `python3-venv` must be installed for PlatformIO's virtual environment.

### Build

```bash
esphome compile nuki-lock-test.yaml
```

The first build downloads the ESP32 platform toolchain (~100s). Subsequent builds are incremental (~30s).

### Lint

ESPHome has no built-in C++ linter. Compilation with `-Wall` catches most issues. The `lock.py` codegen adds `-Wno-unused-result -Wno-ignored-qualifiers -Wno-missing-field-initializers -Wno-maybe-uninitialized` to suppress warnings from the NukiBleEsp32 library.

For Python validation: `esphome config nuki-lock-test.yaml` validates the YAML config.

### Testing

This is a cross-compiled embedded component targeting ESP32-S3 hardware. There is no emulator for the full BLE stack. Testing is done by:
1. Successful compilation (`esphome compile`)
2. Flashing to real hardware and pairing with a Nuki lock

### Key architecture notes

- ESPHome main loop runs on **Core 1** (see `esphome/components/esp32/core.cpp` line 72)
- The Nuki BLE task runs on **Core 0** (pinned via `xTaskCreatePinnedToCore`)
- NimBLE's host task also runs on Core 0, giving BLE affinity
- Communication uses FreeRTOS queues: `cmd_queue_` (Core 1→0) and `result_queue_` (Core 0→1)
- Never call `publish_state()` from the BLE task — all state updates go through the result queue
- The component uses ESP-IDF framework (not Arduino) — required for NimBLE integration

### Gotchas

- The external libraries (NukiBleEsp32, esp-nimble-cpp, etc.) are fetched at build time via `add_idf_component()` in `lock.py`. No manual dependency installation needed.
- PSRAM should be enabled for memory-intensive BLE operations.
- The component is incompatible with ESPHome's built-in BLE stack (`esp32_ble`, `esp32_improv`, etc.) — it uses NimBLE directly.
