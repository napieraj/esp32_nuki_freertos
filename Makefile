SHELL  := /usr/bin/env bash
VENV   := .venv
BIN    := $(VENV)/bin
YAML   := nuki-lock-test.yaml
UART_YAML := nuki-uart-bridge-test.yaml
PY_SRC := components/nuki_pro/*.py components/nuki_uart_bridge/*.py
CXX_SRC:= components/nuki_pro/*.cpp components/nuki_pro/*.h \
          components/nuki_uart_bridge/*.cpp components/nuki_uart_bridge/*.h

.DEFAULT_GOAL := help

# ── Setup ─────────────────────────────────────────────────────────────────────

.PHONY: setup
setup: ## Create venv, install tools, and warm compile cache
	@script/setup

.PHONY: setup-fast
setup-fast: ## Create venv and install tools (skip cache warm compile)
	@ESPHOME_WARM_COMPILE=0 script/setup

# ── Build & validate ──────────────────────────────────────────────────────────

.PHONY: config
config: ## Validate ESPHome YAML config
	$(BIN)/esphome config $(YAML)

.PHONY: compile
compile: ## Compile firmware for ESP32-S3
	$(BIN)/esphome compile $(YAML)

.PHONY: config-uart
config-uart: ## Validate the UART-bridge ESPHome YAML config
	$(BIN)/esphome config $(UART_YAML)

.PHONY: compile-uart
compile-uart: ## Compile the UART-bridge firmware for ESP32-S3
	$(BIN)/esphome compile $(UART_YAML)

# ── Lint ──────────────────────────────────────────────────────────────────────

.PHONY: lint
lint: lint-python lint-cpp ## Run all linters

.PHONY: lint-python
lint-python: ## Lint Python with ruff
	$(BIN)/ruff check $(PY_SRC)

.PHONY: lint-cpp
lint-cpp: ## Check C++ formatting with clang-format
	clang-format --dry-run --Werror $(CXX_SRC)

# ── Format ────────────────────────────────────────────────────────────────────

.PHONY: format
format: format-python format-cpp ## Auto-format all source files

.PHONY: format-python
format-python: ## Auto-fix Python lint issues
	$(BIN)/ruff check --fix $(PY_SRC)

.PHONY: format-cpp
format-cpp: ## Auto-format C++ source files
	clang-format -i $(CXX_SRC)

# ── Host tests ────────────────────────────────────────────────────────────────

TEST_BUILD := .esphome/host-tests

.PHONY: test-host
test-host: ## Build and run the pure-C UART framing tests with the host gcc
	@mkdir -p $(TEST_BUILD)
	gcc -std=c99 -Wall -Wextra -Werror -Icomponents/nuki_uart_bridge \
		tests/test_uart_framing.c -o $(TEST_BUILD)/test_uart_framing
	$(TEST_BUILD)/test_uart_framing
	gcc -std=c99 -Wall -Wextra -Werror -Icomponents/nuki_uart_bridge tests/test_uart_entries.c -o $(TEST_BUILD)/test_uart_entries && $(TEST_BUILD)/test_uart_entries

.PHONY: test-seclink
test-seclink: ## Build and run the sec_link (libsodium) transcript tests with the host g++
	@mkdir -p $(TEST_BUILD)
	g++ -std=c++17 -Wall -Wextra -Werror -Icomponents/nuki_uart_bridge \
		tests/test_seclink_host.cpp components/nuki_uart_bridge/nuki_uart_seclink.cpp \
		$$(pkg-config --cflags --libs libsodium) -o $(TEST_BUILD)/test_seclink_host
	$(TEST_BUILD)/test_seclink_host

# ── Clean ─────────────────────────────────────────────────────────────────────

.PHONY: clean
clean: ## Remove build artifacts (keeps venv and toolchain cache)
	rm -rf .esphome/build

.PHONY: clean-all
clean-all: ## Remove build artifacts, venv, and toolchain cache
	rm -rf .esphome .venv

# ── Help ──────────────────────────────────────────────────────────────────────

.PHONY: help
help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-16s\033[0m %s\n", $$1, $$2}'
