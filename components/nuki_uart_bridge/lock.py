"""ESPHome lock platform for the nRF52840 Nuki UART bridge.

The ESP32 is the *host* side of the COBS/CRC16 UART protocol implemented by
the ``nRF52840_nuki_bridge`` firmware; all BLE and Nuki crypto runs on the
nRF.  This platform only speaks UART, so it coexists with any other ESPHome
component (no NimBLE, no ``esp32_ble*`` conflicts).
"""

import logging

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome import automation
from esphome.components import (
    binary_sensor,
    button,
    lock,
    sensor,
    switch,
    text_sensor,
    uart,
)
from esphome.components import time as time_
from esphome.components.esp32 import add_idf_component
from esphome.const import (
    CONF_ID,
    CONF_PIN,
    CONF_TIME_ID,
    CONF_TRIGGER_ID,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_BATTERY_CHARGING,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_DOOR,
    DEVICE_CLASS_SIGNAL_STRENGTH,
    DEVICE_CLASS_SWITCH,
    DEVICE_CLASS_TAMPER,
    ENTITY_CATEGORY_CONFIG,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_DECIBEL_MILLIWATT,
    UNIT_PERCENT,
)

_LOGGER = logging.getLogger(__name__)

DEPENDENCIES = ["esp32", "uart"]
AUTO_LOAD = ["binary_sensor", "sensor", "text_sensor", "switch", "button"]

nuki_uart_bridge_ns = cg.esphome_ns.namespace("nuki_uart_bridge")
NukiUartBridgeLock = nuki_uart_bridge_ns.class_(
    "NukiUartBridgeLock", lock.Lock, cg.Component, uart.UARTDevice
)
PairingCompleteTrigger = nuki_uart_bridge_ns.class_(
    "PairingCompleteTrigger", automation.Trigger.template(cg.uint32)
)
PairingModeOnTrigger = nuki_uart_bridge_ns.class_(
    "PairingModeOnTrigger", automation.Trigger.template()
)
PairingModeOffTrigger = nuki_uart_bridge_ns.class_(
    "PairingModeOffTrigger", automation.Trigger.template()
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

# Sub-entities
PairingModeSwitch = nuki_uart_bridge_ns.class_(
    "NukiUartBridgePairingModeSwitch", switch.Switch
)
UnpairButton = nuki_uart_bridge_ns.class_("NukiUartBridgeUnpairButton", button.Button)
CalibrationButton = nuki_uart_bridge_ns.class_(
    "NukiUartBridgeCalibrationButton", button.Button
)
RebootButton = nuki_uart_bridge_ns.class_("NukiUartBridgeRebootButton", button.Button)

# Actions / conditions
_PARENTED = cg.Parented.template(NukiUartBridgeLock)
PairAction = nuki_uart_bridge_ns.class_("PairAction", automation.Action, _PARENTED)
UnpairAction = nuki_uart_bridge_ns.class_("UnpairAction", automation.Action, _PARENTED)
SetPairingModeAction = nuki_uart_bridge_ns.class_(
    "SetPairingModeAction", automation.Action, _PARENTED
)
SetSecurityPinAction = nuki_uart_bridge_ns.class_(
    "SetSecurityPinAction", automation.Action, _PARENTED
)
VerifyPinAction = nuki_uart_bridge_ns.class_(
    "VerifyPinAction", automation.Action, _PARENTED
)
UpdateTimeAction = nuki_uart_bridge_ns.class_(
    "UpdateTimeAction", automation.Action, _PARENTED
)
RequestCalibrationAction = nuki_uart_bridge_ns.class_(
    "RequestCalibrationAction", automation.Action, _PARENTED
)
RebootAction = nuki_uart_bridge_ns.class_("RebootAction", automation.Action, _PARENTED)
LockNGoAction = nuki_uart_bridge_ns.class_(
    "LockNGoAction", automation.Action, _PARENTED
)
LockActionAction = nuki_uart_bridge_ns.class_(
    "LockActionAction", automation.Action, _PARENTED
)
ConnectedCondition = nuki_uart_bridge_ns.class_(
    "ConnectedCondition", automation.Condition, _PARENTED
)
PairedCondition = nuki_uart_bridge_ns.class_(
    "PairedCondition", automation.Condition, _PARENTED
)

CONF_DEVICE_TYPE = "device_type"
CONF_PAIR_AS = "pair_as"
CONF_LINK_PROFILE = "link_profile"
CONF_APP_ID = "app_id"
CONF_POLL_INTERVAL = "poll_interval"
CONF_AUTO_PAIR = "auto_pair"
CONF_CONNECTED = "connected"
CONF_PAIRED = "paired"
CONF_RSSI = "rssi"
CONF_DIAGNOSTICS = "diagnostics"
CONF_ON_PAIRING_COMPLETE = "on_pairing_complete"
CONF_ON_PAIRING_MODE_ON = "on_pairing_mode_on"
CONF_ON_PAIRING_MODE_OFF = "on_pairing_mode_off"
CONF_ON_STATE_CHANGE = "on_state_change"
CONF_SECURITY_PIN = "security_pin"
CONF_STATE_POLL_INTERVAL = "state_poll_interval"
CONF_EVENT = "event"
CONF_EVENT_LOG_COUNT = "event_log_count"
CONF_LAST_UNLOCK_USER = "last_unlock_user"
CONF_LAST_LOCK_ACTION = "last_lock_action"
CONF_LAST_LOCK_ACTION_TRIGGER = "last_lock_action_trigger"
CONF_LAST_LOCK_ACTION_COMPLETION_STATUS = "last_lock_action_completion_status"
CONF_DOOR_SENSOR = "door_sensor"
CONF_DOOR_SENSOR_STATE = "door_sensor_state"
CONF_DOOR_SECURITY_STATE = "door_security_state"
CONF_TAMPER = "tamper"
CONF_ON_EVENT_LOG = "on_event_log"
CONF_ON_DOOR_STATE = "on_door_state"
CONF_SECURE_LINK = "secure_link"
CONF_BATTERY_LEVEL = "battery_level"
CONF_BATTERY_CRITICAL = "battery_critical"
CONF_BATTERY_CHARGING = "battery_charging"
CONF_KEYPAD_BATTERY_CRITICAL = "keypad_battery_critical"
CONF_DOOR_SENSOR_BATTERY_CRITICAL = "door_sensor_battery_critical"
CONF_NIGHT_MODE = "night_mode"
CONF_KEYPAD_PAIRED = "keypad_paired"
CONF_PIN_STATUS = "pin_status"
CONF_NUKI_STATE = "nuki_state"
CONF_FIRMWARE_VERSION = "firmware_version"
CONF_HARDWARE_VERSION = "hardware_version"
CONF_LOCK_NAME = "lock_name"
CONF_NUKI_ID = "nuki_id"
CONF_PAIRING_MODE = "pairing_mode"
CONF_PAIRING_MODE_TIMEOUT = "pairing_mode_timeout"
CONF_UNPAIR = "unpair"
CONF_REQUEST_CALIBRATION = "request_calibration"
CONF_REBOOT = "reboot"
CONF_ALLOWED_ACTIONS = "allowed_actions"
CONF_ACTION_SUFFIX = "action_suffix"
CONF_UNLATCH = "unlatch"
CONF_SUFFIX = "suffix"
CONF_ACTION = "action"

# Values are the PAIR payload bytes understood by the bridge.
DEVICE_TYPES = {"auto": 0x00, "classic": 0x01, "ultra": 0x02}
ID_TYPES = {"app": 0x00, "bridge": 0x01}
LINK_PROFILES = {"eco": 0x00, "armed": 0x01}

# allowed_actions names -> ActionBit (nuki_uart_bridge.h) and the UART
# command byte of each action (host-integration.md §6).
ACTION_BITS = {
    "unlock": 1 << 0,
    "lock": 1 << 1,
    "unlatch": 1 << 2,
    "lock_n_go": 1 << 3,
    "lock_n_go_unlatch": 1 << 4,
    "full_lock": 1 << 5,
    "fob_1": 1 << 6,
    "fob_2": 1 << 7,
    "fob_3": 1 << 8,
}
ACTION_CMDS = {
    "unlock": 0x01,
    "lock": 0x02,
    "unlatch": 0x03,
    "lock_n_go": 0x06,
    "lock_n_go_unlatch": 0x08,
    "full_lock": 0x09,
    "fob_1": 0x0A,
    "fob_2": 0x0B,
    "fob_3": 0x0C,
}
ACTION_SUFFIX_MAX = 20  # Lock Action name suffix, spec p.36


def validate_pin(value):
    """Security PIN as a string of 1-6 digits.

    Gen 1-4 locks use a uint16 PIN (up to 65535, so 1-5 digits), the Ultra a
    6-digit uint32 (spec p.16).  Leading zeros are kept in YAML but the PIN
    is encoded numerically ("000548" is the number 548, as the Nuki app does
    it); the wire width is chosen at send time from the device type the
    bridge / Config reports.  `_final_validate` rejects > 65535 for
    `device_type: classic`.
    """
    value = cv.string(value)
    if not value.isdigit() or not 1 <= len(value) <= 6:
        raise cv.Invalid("PIN must be 1 to 6 digits (4 on gen 1-4, 6 on Ultra).")
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


def validate_action_suffix(value):
    value = cv.string_strict(value)
    if len(value.encode("utf-8")) > ACTION_SUFFIX_MAX:
        raise cv.Invalid(f"action_suffix must be at most {ACTION_SUFFIX_MAX} bytes")
    return value


def _diag_binary(device_class=cv.UNDEFINED, icon=cv.UNDEFINED):
    return binary_sensor.binary_sensor_schema(
        device_class=device_class,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        icon=icon,
    )


def _diag_text(icon=cv.UNDEFINED):
    return text_sensor.text_sensor_schema(
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC, icon=icon
    )


CONFIG_SCHEMA = (
    lock.lock_schema(NukiUartBridgeLock)
    .extend(
        {
            cv.Optional(CONF_PIN): validate_pin,  # legacy name of security_pin
            cv.Optional(CONF_SECURITY_PIN): validate_pin,
            cv.Optional(CONF_TIME_ID): cv.use_id(time_.RealTimeClock),
            cv.Optional(
                CONF_STATE_POLL_INTERVAL, default="60s"
            ): cv.positive_time_period_seconds,
            cv.Optional(
                CONF_PAIRING_MODE_TIMEOUT, default="300s"
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
            cv.Optional(CONF_ALLOWED_ACTIONS, default=list(ACTION_BITS)): cv.All(
                cv.ensure_list(cv.enum(ACTION_BITS, lower=True)), cv.Length(min=1)
            ),
            cv.Optional(CONF_ACTION_SUFFIX, default=""): validate_action_suffix,
            # ── diagnostics / link ──
            cv.Optional(CONF_CONNECTED): _diag_binary(DEVICE_CLASS_CONNECTIVITY),
            cv.Optional(CONF_PAIRED): _diag_binary(
                DEVICE_CLASS_CONNECTIVITY, icon="mdi:link"
            ),
            cv.Optional(CONF_RSSI): sensor.sensor_schema(
                unit_of_measurement=UNIT_DECIBEL_MILLIWATT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_SIGNAL_STRENGTH,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_DIAGNOSTICS): _diag_text(),
            cv.Optional(CONF_PIN_STATUS): _diag_text(icon="mdi:shield-key"),
            cv.Optional(CONF_NUKI_STATE): _diag_text(),
            cv.Optional(CONF_FIRMWARE_VERSION): _diag_text(icon="mdi:chip"),
            cv.Optional(CONF_HARDWARE_VERSION): _diag_text(icon="mdi:chip"),
            cv.Optional(CONF_LOCK_NAME): _diag_text(),
            cv.Optional(CONF_NUKI_ID): _diag_text(),
            cv.Optional(CONF_KEYPAD_PAIRED): _diag_binary(icon="mdi:dialpad"),
            # ── battery (Keyturner States bytes 12 / 20) ──
            cv.Optional(CONF_BATTERY_LEVEL): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_BATTERY,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_BATTERY_CRITICAL): _diag_binary(DEVICE_CLASS_BATTERY),
            cv.Optional(CONF_BATTERY_CHARGING): _diag_binary(
                DEVICE_CLASS_BATTERY_CHARGING
            ),
            cv.Optional(CONF_KEYPAD_BATTERY_CRITICAL): _diag_binary(
                DEVICE_CLASS_BATTERY
            ),
            cv.Optional(CONF_DOOR_SENSOR_BATTERY_CRITICAL): _diag_binary(
                DEVICE_CLASS_BATTERY
            ),
            # ── lock / door state ──
            cv.Optional(CONF_LAST_UNLOCK_USER): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_LAST_LOCK_ACTION): text_sensor.text_sensor_schema(),
            cv.Optional(
                CONF_LAST_LOCK_ACTION_TRIGGER
            ): text_sensor.text_sensor_schema(),
            cv.Optional(
                CONF_LAST_LOCK_ACTION_COMPLETION_STATUS
            ): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_NIGHT_MODE): binary_sensor.binary_sensor_schema(
                icon="mdi:weather-night"
            ),
            cv.Optional(CONF_DOOR_SENSOR_STATE): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_DOOR_SECURITY_STATE): text_sensor.text_sensor_schema(
                icon="mdi:door-closed-lock"
            ),
            cv.Optional(CONF_DOOR_SENSOR): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_DOOR,
            ),
            cv.Optional(CONF_TAMPER): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_TAMPER,
            ),
            # ── operator entities ──
            cv.Optional(CONF_PAIRING_MODE): switch.switch_schema(
                PairingModeSwitch,
                device_class=DEVICE_CLASS_SWITCH,
                entity_category=ENTITY_CATEGORY_CONFIG,
                icon="mdi:bluetooth",
                default_restore_mode="ALWAYS_OFF",
            ),
            cv.Optional(CONF_UNPAIR): button.button_schema(
                UnpairButton,
                entity_category=ENTITY_CATEGORY_CONFIG,
                icon="mdi:link-off",
            ),
            cv.Optional(CONF_REQUEST_CALIBRATION): button.button_schema(
                CalibrationButton,
                entity_category=ENTITY_CATEGORY_CONFIG,
                icon="mdi:progress-wrench",
            ),
            cv.Optional(CONF_REBOOT): button.button_schema(
                RebootButton,
                entity_category=ENTITY_CATEGORY_CONFIG,
                icon="mdi:restart",
            ),
            # ── automations ──
            cv.Optional(CONF_ON_PAIRING_COMPLETE): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(PairingCompleteTrigger)}
            ),
            cv.Optional(CONF_ON_PAIRING_MODE_ON): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(PairingModeOnTrigger)}
            ),
            cv.Optional(CONF_ON_PAIRING_MODE_OFF): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(PairingModeOffTrigger)}
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


def _validate_config(config):
    if CONF_PIN in config and CONF_SECURITY_PIN in config:
        raise cv.Invalid(f"Use either '{CONF_SECURITY_PIN}' or '{CONF_PIN}', not both.")
    if config[CONF_STATE_POLL_INTERVAL].total_seconds > 0xFFFF:
        raise cv.Invalid(f"'{CONF_STATE_POLL_INTERVAL}' must be at most 65535 s.")
    pin = config.get(CONF_SECURITY_PIN, config.get(CONF_PIN))
    if pin is not None and config[CONF_DEVICE_TYPE] == "classic" and int(pin) > 0xFFFF:
        raise cv.Invalid(
            "A gen 1-4 lock (device_type: classic) has a 16-bit PIN (at most 65535)."
        )
    if pin is not None and config[CONF_DEVICE_TYPE] == "ultra" and len(pin) != 6:
        raise cv.Invalid("An Ultra (device_type: ultra) has a 6-digit PIN.")
    return config


CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, _validate_config)


def _final_validate(config):
    # 921600 8N1 is the bridge default (boards/xiao_ble.overlay); 115200 is
    # the documented fallback.  Both are accepted; only the direction is
    # enforced.
    uart.final_validate_device_schema(
        "nuki_uart_bridge", require_tx=True, require_rx=True
    )(config)
    full = fv.full_config.get()
    api_conf = full.get("api")
    if api_conf is None:
        return config
    if not api_conf.get("custom_services", False):
        _LOGGER.warning(
            "nuki_uart_bridge: set 'api: custom_services: true' for the "
            "lock_n_go / keypad / update_time / verify_pin services."
        )
    if config[CONF_EVENT] != "none" and not api_conf.get(
        "homeassistant_services", False
    ):
        _LOGGER.warning(
            "nuki_uart_bridge: 'event: %s' needs 'api: homeassistant_services: true'.",
            config[CONF_EVENT],
        )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def _text(config, key, setter):
    if key in config:
        sens = await text_sensor.new_text_sensor(config[key])
        cg.add(setter(sens))


async def _binary(config, key, setter):
    if key in config:
        sens = await binary_sensor.new_binary_sensor(config[key])
        cg.add(setter(sens))


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
    if CONF_TIME_ID in config:
        t = await cg.get_variable(config[CONF_TIME_ID])
        cg.add(var.set_time(t))
    cg.add(
        var.set_state_poll_interval(int(config[CONF_STATE_POLL_INTERVAL].total_seconds))
    )
    cg.add(
        var.set_pairing_mode_timeout(
            int(config[CONF_PAIRING_MODE_TIMEOUT].total_seconds)
        )
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
    mask = 0
    for action in config[CONF_ALLOWED_ACTIONS]:
        mask |= ACTION_BITS[str(action)]
    cg.add(var.set_allowed_actions(mask))
    cg.add(var.set_action_suffix(config[CONF_ACTION_SUFFIX]))

    await _binary(config, CONF_CONNECTED, var.set_connected_binary_sensor)
    await _binary(config, CONF_PAIRED, var.set_paired_binary_sensor)
    if CONF_RSSI in config:
        sens = await sensor.new_sensor(config[CONF_RSSI])
        cg.add(var.set_rssi_sensor(sens))
    if CONF_BATTERY_LEVEL in config:
        sens = await sensor.new_sensor(config[CONF_BATTERY_LEVEL])
        cg.add(var.set_battery_level_sensor(sens))
    await _binary(config, CONF_BATTERY_CRITICAL, var.set_battery_critical_binary_sensor)
    await _binary(config, CONF_BATTERY_CHARGING, var.set_battery_charging_binary_sensor)
    await _binary(
        config,
        CONF_KEYPAD_BATTERY_CRITICAL,
        var.set_keypad_battery_critical_binary_sensor,
    )
    await _binary(
        config,
        CONF_DOOR_SENSOR_BATTERY_CRITICAL,
        var.set_door_sensor_battery_critical_binary_sensor,
    )
    await _binary(config, CONF_NIGHT_MODE, var.set_night_mode_binary_sensor)
    await _binary(config, CONF_KEYPAD_PAIRED, var.set_keypad_paired_binary_sensor)
    await _binary(config, CONF_DOOR_SENSOR, var.set_door_sensor_binary_sensor)
    await _binary(config, CONF_TAMPER, var.set_tamper_binary_sensor)

    await _text(config, CONF_DIAGNOSTICS, var.set_diagnostics_text_sensor)
    await _text(config, CONF_PIN_STATUS, var.set_pin_status_text_sensor)
    await _text(config, CONF_NUKI_STATE, var.set_nuki_state_text_sensor)
    await _text(config, CONF_FIRMWARE_VERSION, var.set_firmware_version_text_sensor)
    await _text(config, CONF_HARDWARE_VERSION, var.set_hardware_version_text_sensor)
    await _text(config, CONF_LOCK_NAME, var.set_lock_name_text_sensor)
    await _text(config, CONF_NUKI_ID, var.set_nuki_id_text_sensor)
    await _text(config, CONF_LAST_UNLOCK_USER, var.set_last_unlock_user_text_sensor)
    await _text(config, CONF_LAST_LOCK_ACTION, var.set_last_lock_action_text_sensor)
    await _text(
        config,
        CONF_LAST_LOCK_ACTION_TRIGGER,
        var.set_last_lock_action_trigger_text_sensor,
    )
    await _text(
        config,
        CONF_LAST_LOCK_ACTION_COMPLETION_STATUS,
        var.set_last_lock_action_completion_status_text_sensor,
    )
    await _text(config, CONF_DOOR_SENSOR_STATE, var.set_door_sensor_state_text_sensor)
    await _text(
        config, CONF_DOOR_SECURITY_STATE, var.set_door_security_state_text_sensor
    )

    if CONF_PAIRING_MODE in config:
        s = await switch.new_switch(config[CONF_PAIRING_MODE])
        await cg.register_parented(s, config[CONF_ID])
        cg.add(var.set_pairing_mode_switch(s))
    for key in (CONF_UNPAIR, CONF_REQUEST_CALIBRATION, CONF_REBOOT):
        if key in config:
            b = await button.new_button(config[key])
            await cg.register_parented(b, config[CONF_ID])

    for conf in config.get(CONF_ON_PAIRING_COMPLETE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.uint32, "auth_id")], conf)
    for conf in config.get(CONF_ON_PAIRING_MODE_ON, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)
    for conf in config.get(CONF_ON_PAIRING_MODE_OFF, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)
    for conf in config.get(CONF_ON_STATE_CHANGE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.uint8, "lock_state")], conf)
    for conf in config.get(CONF_ON_EVENT_LOG, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(NukiLogEntry, "entry")], conf)
    for conf in config.get(CONF_ON_DOOR_STATE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.uint8, "door_state")], conf)


# ── Actions and conditions (nuki_uart_bridge.<name>) ─────────────────────

_ACTION_SCHEMA = automation.maybe_simple_id(
    {cv.GenerateID(): cv.use_id(NukiUartBridgeLock)}
)


async def _simple_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


for _name, _cls in (
    ("pair", PairAction),
    ("unpair", UnpairAction),
    ("verify_pin", VerifyPinAction),
    ("update_time", UpdateTimeAction),
    ("request_calibration", RequestCalibrationAction),
    ("reboot", RebootAction),
):
    automation.register_action(
        f"nuki_uart_bridge.{_name}", _cls, _ACTION_SCHEMA, synchronous=True
    )(_simple_action_to_code)


@automation.register_action(
    "nuki_uart_bridge.set_pairing_mode",
    SetPairingModeAction,
    automation.maybe_simple_id(
        {
            cv.GenerateID(): cv.use_id(NukiUartBridgeLock),
            cv.Required(CONF_PAIRING_MODE): cv.templatable(cv.boolean),
        }
    ),
    synchronous=True,
)
async def set_pairing_mode_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    tmpl = await cg.templatable(config[CONF_PAIRING_MODE], args, cg.bool_)
    cg.add(var.set_pairing_mode(tmpl))
    return var


@automation.register_action(
    "nuki_uart_bridge.set_security_pin",
    SetSecurityPinAction,
    automation.maybe_simple_id(
        {
            cv.GenerateID(): cv.use_id(NukiUartBridgeLock),
            cv.Required(CONF_SECURITY_PIN): cv.templatable(cv.int_range(0, 999999)),
        }
    ),
    synchronous=True,
)
async def set_security_pin_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    tmpl = await cg.templatable(config[CONF_SECURITY_PIN], args, cg.uint32)
    cg.add(var.set_pin(tmpl))
    return var


@automation.register_action(
    "nuki_uart_bridge.lock_n_go",
    LockNGoAction,
    automation.maybe_simple_id(
        {
            cv.GenerateID(): cv.use_id(NukiUartBridgeLock),
            cv.Optional(CONF_UNLATCH, default=False): cv.templatable(cv.boolean),
            cv.Optional(CONF_SUFFIX, default=""): cv.templatable(cv.string),
        }
    ),
    synchronous=True,
)
async def lock_n_go_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    tmpl = await cg.templatable(config[CONF_UNLATCH], args, cg.bool_)
    cg.add(var.set_unlatch(tmpl))
    tmpl = await cg.templatable(config[CONF_SUFFIX], args, cg.std_string)
    cg.add(var.set_suffix(tmpl))
    return var


@automation.register_action(
    "nuki_uart_bridge.lock_action",
    LockActionAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(NukiUartBridgeLock),
            cv.Required(CONF_ACTION): cv.templatable(cv.enum(ACTION_CMDS, lower=True)),
            cv.Optional(CONF_SUFFIX, default=""): cv.templatable(cv.string),
        }
    ),
    synchronous=True,
)
async def lock_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    tmpl = await cg.templatable(config[CONF_ACTION], args, cg.uint8)
    cg.add(var.set_action(tmpl))
    tmpl = await cg.templatable(config[CONF_SUFFIX], args, cg.std_string)
    cg.add(var.set_suffix(tmpl))
    return var


_CONDITION_SCHEMA = automation.maybe_simple_id(
    {cv.GenerateID(): cv.use_id(NukiUartBridgeLock)}
)


async def _condition_to_code(config, condition_id, template_arg, args):
    var = cg.new_Pvariable(condition_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


automation.register_condition(
    "nuki_uart_bridge.connected", ConnectedCondition, _CONDITION_SCHEMA
)(_condition_to_code)
automation.register_condition(
    "nuki_uart_bridge.paired", PairedCondition, _CONDITION_SCHEMA
)(_condition_to_code)
