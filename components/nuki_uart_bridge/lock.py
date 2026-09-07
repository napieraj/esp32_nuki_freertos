"""ESPHome lock platform for the nRF52840 Nuki UART bridge.

The ESP32 is the *host* side of the COBS/CRC16 UART protocol implemented by
the ``nRF52840_nuki_bridge`` firmware; all BLE and Nuki crypto runs on the
nRF.  This platform only speaks UART, so it coexists with any other ESPHome
component (no NimBLE, no ``esp32_ble*`` conflicts).
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import binary_sensor, lock, sensor, text_sensor, uart
from esphome.const import (
    CONF_PIN,
    CONF_TRIGGER_ID,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_SIGNAL_STRENGTH,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_DECIBEL_MILLIWATT,
)

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["binary_sensor", "sensor", "text_sensor"]

nuki_uart_bridge_ns = cg.esphome_ns.namespace("nuki_uart_bridge")
NukiUartBridgeLock = nuki_uart_bridge_ns.class_(
    "NukiUartBridgeLock", lock.Lock, cg.Component, uart.UARTDevice
)
PairingCompleteTrigger = nuki_uart_bridge_ns.class_(
    "PairingCompleteTrigger", automation.Trigger.template(cg.uint32)
)
StateChangeTrigger = nuki_uart_bridge_ns.class_(
    "StateChangeTrigger", automation.Trigger.template(cg.uint8)
)

CONF_DEVICE_TYPE = "device_type"
CONF_PAIR_AS = "pair_as"
CONF_LINK_PROFILE = "link_profile"
CONF_APP_ID = "app_id"
CONF_POLL_INTERVAL = "poll_interval"
CONF_AUTO_PAIR = "auto_pair"
CONF_CONNECTED = "connected"
CONF_RSSI = "rssi"
CONF_DIAGNOSTICS = "diagnostics"
CONF_ON_PAIRING_COMPLETE = "on_pairing_complete"
CONF_ON_STATE_CHANGE = "on_state_change"

# Values are the PAIR payload bytes understood by the bridge.
DEVICE_TYPES = {"auto": 0x00, "classic": 0x01, "ultra": 0x02}
ID_TYPES = {"app": 0x00, "bridge": 0x01}
LINK_PROFILES = {"eco": 0x00, "armed": 0x01}


def validate_pin(value):
    value = cv.string(value)
    if not value.isdigit() or len(value) != 6:
        raise cv.Invalid("PIN must be exactly 6 digits (leading zeros kept).")
    return value


CONFIG_SCHEMA = (
    lock.lock_schema(NukiUartBridgeLock)
    .extend(
        {
            cv.Optional(CONF_PIN): validate_pin,
            cv.Optional(CONF_DEVICE_TYPE, default="auto"): cv.enum(
                DEVICE_TYPES, lower=True
            ),
            cv.Optional(CONF_PAIR_AS, default="app"): cv.enum(ID_TYPES, lower=True),
            cv.Optional(CONF_LINK_PROFILE, default="armed"): cv.enum(
                LINK_PROFILES, lower=True
            ),
            cv.Optional(CONF_APP_ID): cv.uint32_t,
            cv.Optional(
                CONF_POLL_INTERVAL, default="60s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_AUTO_PAIR, default=True): cv.boolean,
            cv.Optional(CONF_CONNECTED): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_CONNECTIVITY,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_RSSI): sensor.sensor_schema(
                unit_of_measurement=UNIT_DECIBEL_MILLIWATT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_SIGNAL_STRENGTH,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_DIAGNOSTICS): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_ON_PAIRING_COMPLETE): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(PairingCompleteTrigger)}
            ),
            cv.Optional(CONF_ON_STATE_CHANGE): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(StateChangeTrigger)}
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

# 921600 8N1 is the bridge default (boards/xiao_ble.overlay); 115200 is the
# documented fallback.  Both are accepted here; only the direction is enforced.
FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "nuki_uart_bridge", require_tx=True, require_rx=True
)


async def to_code(config):
    var = await lock.new_lock(config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_PIN in config:
        cg.add(var.set_pin(int(config[CONF_PIN])))
    cg.add(var.set_device_type(config[CONF_DEVICE_TYPE]))
    cg.add(var.set_pair_as(config[CONF_PAIR_AS]))
    cg.add(var.set_link_profile(config[CONF_LINK_PROFILE]))
    if CONF_APP_ID in config:
        cg.add(var.set_app_id(config[CONF_APP_ID]))
    cg.add(var.set_poll_interval(config[CONF_POLL_INTERVAL]))
    cg.add(var.set_auto_pair(config[CONF_AUTO_PAIR]))

    if CONF_CONNECTED in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_CONNECTED])
        cg.add(var.set_connected_binary_sensor(sens))
    if CONF_RSSI in config:
        sens = await sensor.new_sensor(config[CONF_RSSI])
        cg.add(var.set_rssi_sensor(sens))
    if CONF_DIAGNOSTICS in config:
        sens = await text_sensor.new_text_sensor(config[CONF_DIAGNOSTICS])
        cg.add(var.set_diagnostics_text_sensor(sens))

    for conf in config.get(CONF_ON_PAIRING_COMPLETE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.uint32, "auth_id")], conf)
    for conf in config.get(CONF_ON_STATE_CHANGE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.uint8, "lock_state")], conf)
