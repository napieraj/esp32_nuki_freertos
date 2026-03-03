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

### Make targets

| Target | Description |
|--------|-------------|
| `make setup` | Create venv and install ESPHome + dev tools |
| `make config` | Validate ESPHome YAML config |
| `make compile` | Compile firmware for ESP32-S3 |
| `make lint` | Run all linters (ruff + clang-format) |
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
