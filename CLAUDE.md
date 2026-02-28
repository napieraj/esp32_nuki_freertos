# CLAUDE.md - ESP32 Nuki FreeRTOS Architecture

## 1. Project Identity & Core Philosophy

- **Repository:** `napieraj/esp32_nuki_freertos`
- **Goal:** An advanced, memory-safe ESPHome custom component bridging Nuki Smart Locks (Gen 2-4 Pro, and Gen 5 Ultra) via BLE.
- **The Problem:** Standard ESPHome BLE integrations run synchronously in the cooperative `loop()`. Nuki's heavy Elliptic Curve Cryptography (Curve25519) and blocking BLE handshakes cause ESP32 Watchdog Timers (WDT) to trip, dropping Wi-Fi/MQTT.
- **The Solution:** This project aggressively decouples NimBLE communication and NaCl Crypto from the main ESPHome loop using **FreeRTOS SMP**.

## 2. Hardware & Framework (2026 Standards)

- **Target:** ESP32 / ESP32-S3 / ESP32-C3 / ESP32-C6.
- **Framework:** ESP-IDF v5.4+ (Arduino Core is disabled/deprecated for this component to save RAM).
- **BLE Stack:** ESP-IDF NimBLE natively.

## 3. Threading Model

1. **ESPHome Main Thread (Core 1 - APP_CPU):** Handles Home Assistant API, MQTT, and exposes the `esphome::lock::Lock` platform. **NEVER BLOCK THIS THREAD.**
2. **Nuki Worker Task (Core 0 - PRO_CPU):** A dedicated FreeRTOS task (`xTaskCreatePinnedToCore`) handling BLE advertisements, MTU negotiation, and NaCl payloads.
3. **IPC (Inter-Process Communication):** Thread-safe communication managed via FreeRTOS `QueueHandle_t`.

## 4. Build & Lint Commands

- **Compile:** `esphome compile nuki-lock-test.yaml`
- **Validate YAML:** `esphome config nuki-lock-test.yaml`
- **Upload:** `esphome run nuki-lock-test.yaml --device <IP>`
- **C++ Format:** `clang-format -i components/nuki_pro/*.cpp components/nuki_pro/*.h`
- **Python Format:** `ruff check components/nuki_pro/*.py --fix`

## 5. Key Files

- `components/nuki_pro/lock.py` — ESPHome component registration, PIN validation, IDF component deps
- `components/nuki_pro/nuki_pro.h` — FreeRTOS primitives, atomic state, class definition
- `components/nuki_pro/nuki_pro.cpp` — BLE task with `xQueueReceive` hybrid engine
- `nuki-lock-test.yaml` — Test config for ESP32-S3
