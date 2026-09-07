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

} // namespace nuki_uart_bridge
} // namespace esphome
