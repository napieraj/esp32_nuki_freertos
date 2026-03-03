import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import lock
from esphome.components.esp32 import (
    add_idf_component,
    add_idf_sdkconfig_option,
    include_builtin_idf_component,
)
from esphome.const import CONF_ID, CONF_PIN
import esphome.final_validate as fv
from esphome.core import CORE
import logging

LOGGER = logging.getLogger(__name__)

DEPENDENCIES = ["esp32"]

nuki_pro_ns = cg.esphome_ns.namespace("nuki_pro")
NukiProLock = nuki_pro_ns.class_("NukiProLock", lock.Lock, cg.Component)

CONF_POLL_INTERVAL = "poll_interval"
CONF_KEEPALIVE = "keepalive"
CONF_NUKI_ID = "nuki_id"


def validate_pin(value):
    value = cv.string(value)
    if not value.isdigit() or len(value) != 6:
        raise cv.Invalid("PIN must be exactly 6 digits for Nuki 5.0 Pro.")
    return value


CONFIG_SCHEMA = lock.lock_schema(NukiProLock).extend({
    cv.Required(CONF_PIN): validate_pin,
    cv.Optional(CONF_NUKI_ID, default=2020002): cv.uint32_t,
    cv.Optional(CONF_POLL_INTERVAL, default="100ms"): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_KEEPALIVE, default=True): cv.boolean,
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await lock.new_lock(config)
    await cg.register_component(var, config)

    cg.add(var.set_pin(int(config[CONF_PIN])))
    cg.add(var.set_nuki_id(config[CONF_NUKI_ID]))
    cg.add(var.set_poll_interval(config[CONF_POLL_INTERVAL]))
    cg.add(var.set_keepalive(config[CONF_KEEPALIVE]))

    # ── NimBLE + Nuki BLE libraries ─────────────────────────────────
    add_idf_component(name="espressif/libsodium", ref="^1.0.20~2")
    add_idf_component(name="crc16", repo="https://github.com/AzonInc/Crc16.git")
    add_idf_component(name="ble-scanner", repo="https://github.com/AzonInc/ble-scanner.git", ref="2.1.0")
    add_idf_component(name="esp-nimble-cpp", repo="https://github.com/h2zero/esp-nimble-cpp.git", ref="2.3.3")
    add_idf_component(name="NukiBleEsp32", repo="https://github.com/AzonInc/NukiBleEsp32.git", ref="idf")

    # ── NimBLE stack ────────────────────────────────────────────────
    add_idf_sdkconfig_option("CONFIG_BT_ENABLED", True)
    add_idf_sdkconfig_option("CONFIG_BT_BLUEDROID_ENABLED", False)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_ENABLED", True)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_ROLE_PERIPHERAL", True)
    add_idf_sdkconfig_option("CONFIG_BTDM_BLE_SCAN_DUPL", True)
    add_idf_sdkconfig_option("CONFIG_NIMBLE_CPP_LOG_LEVEL", 0)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_LOG_LEVEL", 0)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_LOG_LEVEL_NONE", True)

    # ── Core isolation: move TCP/IP (LwIP) to Core 1 ───────────────
    # Keeps Core 0 exclusive for BLE — critical for PoE boards where
    # Ethernet MAC interrupts would otherwise cause BLE jitter.
    # NOTE: USE_ETHERNET is defined by the ethernet component itself;
    # we intentionally do NOT set it here so the lock component
    # remains compatible with WiFi-only configurations.
    add_idf_sdkconfig_option("CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU1", True)

    # ── Selective compilation: keep IDF components for PoE boards ──
    # Prevents build from stripping network/storage drivers that
    # Ethernet-equipped boards depend on
    include_builtin_idf_component("esp_eth")
    include_builtin_idf_component("esp_vfs_fat")

    cg.add_define("NUKI_NO_WDT_RESET")
    cg.add_build_flag("-Wno-unused-result")
    cg.add_build_flag("-Wno-ignored-qualifiers")
    cg.add_build_flag("-Wno-missing-field-initializers")
    cg.add_build_flag("-Wno-maybe-uninitialized")


def _final_validate(config):
    full_config = fv.full_config.get()
    incompatible = [
        "esp32_ble", "esp32_improv", "esp32_ble_beacon",
        "esp32_ble_client", "esp32_ble_tracker", "esp32_ble_server",
    ]
    if CORE.is_esp32:
        if any(c in full_config for c in incompatible):
            raise cv.Invalid(
                "nuki_pro uses NimBLE directly — remove all esp32_ble* components."
            )
        if "psram" in full_config:
            add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL", True)
            add_idf_sdkconfig_option("CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL", 50768)
            add_idf_sdkconfig_option("CONFIG_BT_ALLOCATION_FROM_SPIRAM_FIRST", True)
            add_idf_sdkconfig_option("CONFIG_BT_BLE_DYNAMIC_ENV_MEMORY", True)
        else:
            LOGGER.warning("Consider enabling PSRAM for NimBLE stack.")

        # Enforce sane keepalive bounds
        if config.get(CONF_KEEPALIVE, True):
            poll_ms = int(config.get(CONF_POLL_INTERVAL, 100).total_milliseconds)
            if poll_ms > 2000:
                LOGGER.warning(
                    "keepalive is enabled but poll_interval (%dms) exceeds 2000ms. "
                    "The Nuki lock disconnects idle BLE after ~10s. Consider ≤500ms.",
                    poll_ms,
                )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate
