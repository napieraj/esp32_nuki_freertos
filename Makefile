SHELL  := /usr/bin/env bash
VENV   := .venv
BIN    := $(VENV)/bin
YAML   := nuki-lock-test.yaml
PY_SRC := components/nuki_pro/*.py
CXX_SRC:= components/nuki_pro/*.cpp components/nuki_pro/*.h

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
