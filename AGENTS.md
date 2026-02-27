# AGENTS.md

## Project Overview

ESP32 firmware project for Nuki smart lock integration, built with ESP-IDF (v5.4) and FreeRTOS. This is a cross-compiled embedded C project targeting the Xtensa ESP32 architecture.

## Cursor Cloud specific instructions

### Environment

- ESP-IDF v5.4 is installed at `/opt/esp-idf`. Before running any `idf.py` command, source the environment: `. /opt/esp-idf/export.sh`.
- This is already added to `~/.bashrc` so it loads automatically in new shell sessions.
- The Xtensa cross-compiler toolchain is installed at `~/.espressif/tools/`.
- QEMU for ESP32 (Xtensa) is installed at `~/.espressif/tools/qemu-xtensa/` for running firmware without hardware.

### Build

```bash
. /opt/esp-idf/export.sh
cd /workspace
idf.py build
```

### Run in QEMU (no hardware required)

After building, create a 4MB flash image and run:

```bash
python3 -m esptool --chip esp32 merge_bin -o build/merged_fw.bin --flash_mode dio --flash_size 4MB \
  0x1000 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/esp32_nuki_freertos.bin

dd if=/dev/zero bs=1M count=4 of=build/flash_image.bin
dd if=build/merged_fw.bin of=build/flash_image.bin conv=notrunc

qemu-system-xtensa -nographic -machine esp32 \
  -drive file=build/flash_image.bin,if=mtd,format=raw -no-reboot
```

### Lint / Static Analysis

ESP-IDF does not include a built-in linter. Use `idf.py build` with warnings treated as errors for compile-time checks. For C linting, `clang-tidy` can be used if installed.

### Testing

ESP-IDF includes a Unity-based unit testing framework. Tests use the `TEST_CASE` macro and are compiled as a separate test app. See ESP-IDF docs on unit testing for details.

### Gotchas

- QEMU needs `libslirp0` installed (`sudo apt-get install -y libslirp0`) or it will fail with a shared library error.
- Flash images for QEMU must be exactly 2, 4, 8, or 16 MB. Pad with `dd` from `/dev/zero` before overlaying the merged binary.
- `sdkconfig` is generated at build time and excluded from version control. Use `sdkconfig.defaults` for persistent configuration.
