#pragma once

#include "esphome/core/automation.h"
#include "nuki_uart_bridge.h"

namespace esphome {
namespace nuki_uart_bridge {

/// Fires once the bridge reports PAIRING_COMPLETE; x = authorization id.
class PairingCompleteTrigger : public Trigger<uint32_t> {
public:
  explicit PairingCompleteTrigger(NukiUartBridgeLock *parent) {
    parent->add_on_pairing_complete_callback(
        [this](uint32_t auth_id) { this->trigger(auth_id); });
  }
};

/// Fires on every Keyturner States update; x = raw Nuki lock state byte.
class StateChangeTrigger : public Trigger<uint8_t> {
public:
  explicit StateChangeTrigger(NukiUartBridgeLock *parent) {
    parent->add_on_state_change_callback(
        [this](uint8_t lock_state) { this->trigger(lock_state); });
  }
};

/// Fires for every new Log Entry (index above the last one seen);
/// x = parsed nuki_log_entry_t (index, ts, auth_id, name, type, data).
class EventLogTrigger : public Trigger<nuki_log_entry_t> {
public:
  explicit EventLogTrigger(NukiUartBridgeLock *parent) {
    parent->add_on_event_log_callback(
        [this](nuki_log_entry_t entry) { this->trigger(entry); });
  }
};

/// Fires when the Keyturner States door sensor byte changes; x = raw state
/// (0x02 closed, 0x03 opened, 0xF0 tampered, ... spec p.32).
class DoorStateTrigger : public Trigger<uint8_t> {
public:
  explicit DoorStateTrigger(NukiUartBridgeLock *parent) {
    parent->add_on_door_state_callback(
        [this](uint8_t state) { this->trigger(state); });
  }
};

} // namespace nuki_uart_bridge
} // namespace esphome
