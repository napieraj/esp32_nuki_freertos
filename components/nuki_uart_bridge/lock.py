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
from esphome.components.esp32 import add_idf_component
from esphome.const import (
    CONF_PIN,
    CONF_TRIGGER_ID,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_DOOR,
    DEVICE_CLASS_SIGNAL_STRENGTH,
    DEVICE_CLASS_TAMPER,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_DECIBEL_MILLIWATT,
)

DEPENDENCIES = ["esp32", "uart"]
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
# Parsed Log Entry (nuki_uart_entries.h, C struct in the global namespace).
NukiLogEntry = cg.global_ns.struct("nuki_log_entry_t")
EventLogTrigger = nuki_uart_bridge_ns.class_(
    "EventLogTrigger", automation.Trigger.template(NukiLogEntry)
)
DoorStateTrigger = nuki_uart_bridge_ns.class_(
    "DoorStateTrigger", automation.Trigger.template(cg.uint8)
)
SecureLinkMode = nuki_uart_bridge_ns.enum("SecureLinkMode", is_class=True)
SECURE_LINK_MODES = {
    False: SecureLinkMode.OFF,
    "auto": SecureLinkMode.AUTO,
    True: SecureLinkMode.ON,
}

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
CONF_SECURITY_PIN = "security_pin"
CONF_STATE_POLL_INTERVAL = "state_poll_interval"
CONF_EVENT = "event"
CONF_EVENT_LOG_COUNT = "event_log_count"
CONF_LAST_UNLOCK_USER = "last_unlock_user"
CONF_LAST_LOCK_ACTION = "last_lock_action"
CONF_LAST_LOCK_ACTION_TRIGGER = "last_lock_action_trigger"
CONF_DOOR_SENSOR = "door_sensor"
CONF_DOOR_SENSOR_STATE = "door_sensor_state"
CONF_TAMPER = "tamper"
CONF_ON_EVENT_LOG = "on_event_log"
CONF_ON_DOOR_STATE = "on_door_state"
CONF_SECURE_LINK = "secure_link"

# Values are the PAIR payload bytes understood by the bridge.
DEVICE_TYPES = {"auto": 0x00, "classic": 0x01, "ultra": 0x02}
ID_TYPES = {"app": 0x00, "bridge": 0x01}
LINK_PROFILES = {"eco": 0x00, "armed": 0x01}


def validate_pin(value):
    """Security PIN as a string of 4-6 digits (gen 1-4: 4, Ultra: 6).

    Leading zeros are kept in YAML but the PIN is encoded numerically; the
    wire width (uint16 / uint32, spec p.16) is chosen at send time from the
    device type the bridge reports.
    """
    value = cv.string(value)
    if not value.isdigit() or not 4 <= len(value) <= 6:
        raise cv.Invalid("PIN must be 4 to 6 digits (4 on gen 1-4, 6 on Ultra).")
    return value


def validate_secure_link(value):
    """true (required) | false (off) | auto (use it when the bridge has it)."""
    if isinstance(value, str) and value.lower() == "auto":
        return "auto"
    return cv.boolean(value)


def validate_event(value):
    value = cv.string_strict(value)
    if value != "none" and not value.replace("_", "").isalnum():
        raise cv.Invalid("event must be an identifier (fired as esphome.<event>)")
    return value


CONFIG_SCHEMA = (
    lock.lock_schema(NukiUartBridgeLock)
    .extend(
        {
            cv.Optional(CONF_PIN): validate_pin,  # legacy name of security_pin
            cv.Optional(CONF_SECURITY_PIN): validate_pin,
            cv.Optional(
                CONF_STATE_POLL_INTERVAL, default="60s"
            ): cv.positive_time_period_seconds,
            cv.Optional(CONF_EVENT, default="none"): validate_event,
            cv.Optional(CONF_SECURE_LINK, default="auto"): validate_secure_link,
            cv.Optional(CONF_EVENT_LOG_COUNT, default=5): cv.int_range(min=1, max=50),
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
            cv.Optional(CONF_LAST_UNLOCK_USER): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_LAST_LOCK_ACTION): text_sensor.text_sensor_schema(),
            cv.Optional(
                CONF_LAST_LOCK_ACTION_TRIGGER
            ): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_DOOR_SENSOR_STATE): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_DOOR_SENSOR): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_DOOR,
            ),
            cv.Optional(CONF_TAMPER): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_TAMPER,
            ),
            cv.Optional(CONF_ON_PAIRING_COMPLETE): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(PairingCompleteTrigger)}
            ),
            cv.Optional(CONF_ON_STATE_CHANGE): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(StateChangeTrigger)}
            ),
            cv.Optional(CONF_ON_EVENT_LOG): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(EventLogTrigger)}
            ),
            cv.Optional(CONF_ON_DOOR_STATE): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(DoorStateTrigger)}
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


def _validate_single_pin(config):
    if CONF_PIN in config and CONF_SECURITY_PIN in config:
        raise cv.Invalid(f"Use either '{CONF_SECURITY_PIN}' or '{CONF_PIN}', not both.")
    if config[CONF_STATE_POLL_INTERVAL].total_seconds > 0xFFFF:
        raise cv.Invalid(f"'{CONF_STATE_POLL_INTERVAL}' must be at most 65535 s.")
    return config


CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, _validate_single_pin)

# 921600 8N1 is the bridge default (boards/xiao_ble.overlay); 115200 is the
# documented fallback.  Both are accepted here; only the direction is enforced.
FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "nuki_uart_bridge", require_tx=True, require_rx=True
)


async def to_code(config):
    # nuki_uart_seclink.cpp (X25519 / BLAKE2b / XChaCha20-Poly1305) needs
    # libsodium even when secure_link is off; the IDF component is small.
    add_idf_component(name="espressif/libsodium", ref="^1.0.20~2")

    var = await lock.new_lock(config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    pin = config.get(CONF_SECURITY_PIN, config.get(CONF_PIN))
    if pin is not None:
        cg.add(var.set_pin(int(pin)))
    cg.add(
        var.set_state_poll_interval(int(config[CONF_STATE_POLL_INTERVAL].total_seconds))
    )
    cg.add(var.set_event("esphome." + config[CONF_EVENT]))
    cg.add(var.set_secure_link(SECURE_LINK_MODES[config[CONF_SECURE_LINK]]))
    cg.add(var.set_event_log_count(config[CONF_EVENT_LOG_COUNT]))
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
    if CONF_LAST_UNLOCK_USER in config:
        sens = await text_sensor.new_text_sensor(config[CONF_LAST_UNLOCK_USER])
        cg.add(var.set_last_unlock_user_text_sensor(sens))
    if CONF_LAST_LOCK_ACTION in config:
        sens = await text_sensor.new_text_sensor(config[CONF_LAST_LOCK_ACTION])
        cg.add(var.set_last_lock_action_text_sensor(sens))
    if CONF_LAST_LOCK_ACTION_TRIGGER in config:
        sens = await text_sensor.new_text_sensor(config[CONF_LAST_LOCK_ACTION_TRIGGER])
        cg.add(var.set_last_lock_action_trigger_text_sensor(sens))
    if CONF_DOOR_SENSOR_STATE in config:
        sens = await text_sensor.new_text_sensor(config[CONF_DOOR_SENSOR_STATE])
        cg.add(var.set_door_sensor_state_text_sensor(sens))
    if CONF_DOOR_SENSOR in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_DOOR_SENSOR])
        cg.add(var.set_door_sensor_binary_sensor(sens))
    if CONF_TAMPER in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_TAMPER])
        cg.add(var.set_tamper_binary_sensor(sens))

    for conf in config.get(CONF_ON_PAIRING_COMPLETE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.uint32, "auth_id")], conf)
    for conf in config.get(CONF_ON_STATE_CHANGE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.uint8, "lock_state")], conf)
    for conf in config.get(CONF_ON_EVENT_LOG, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(NukiLogEntry, "entry")], conf)
    for conf in config.get(CONF_ON_DOOR_STATE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.uint8, "door_state")], conf)
