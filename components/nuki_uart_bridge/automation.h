#pragma once

#include "esphome/core/automation.h"
#include "nuki_uart_bridge.h"

namespace esphome {
namespace nuki_uart_bridge {

// ── Triggers ──────────────────────────────────────────────────────────

/// Fires once the bridge reports PAIRING_COMPLETE; x = authorization id.
class PairingCompleteTrigger : public Trigger<uint32_t> {
public:
  explicit PairingCompleteTrigger(NukiUartBridgeLock *parent) {
    parent->add_on_pairing_complete_callback(
        [this](uint32_t auth_id) { this->trigger(auth_id); });
  }
};

/// Fires when the pairing_mode switch / set_pairing_mode turns on.
class PairingModeOnTrigger : public Trigger<> {
public:
  explicit PairingModeOnTrigger(NukiUartBridgeLock *parent) {
    parent->add_on_pairing_mode_on_callback([this]() { this->trigger(); });
  }
};

/// Fires when pairing mode ends (paired, timeout or switched off).
class PairingModeOffTrigger : public Trigger<> {
public:
  explicit PairingModeOffTrigger(NukiUartBridgeLock *parent) {
    parent->add_on_pairing_mode_off_callback([this]() { this->trigger(); });
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

// ── Actions ───────────────────────────────────────────────────────────

template <typename... Ts>
class PairAction : public Action<Ts...>, public Parented<NukiUartBridgeLock> {
public:
  void play(const Ts &...x) override { this->parent_->pair(); }
};

template <typename... Ts>
class UnpairAction : public Action<Ts...>, public Parented<NukiUartBridgeLock> {
public:
  void play(const Ts &...x) override { this->parent_->unpair(); }
};

template <typename... Ts>
class SetPairingModeAction : public Action<Ts...>,
                             public Parented<NukiUartBridgeLock> {
  TEMPLATABLE_VALUE(bool, pairing_mode)
public:
  void play(const Ts &...x) override {
    this->parent_->set_pairing_mode(this->pairing_mode_.value(x...));
  }
};

/// Runtime PIN override, persisted; 0 clears it (back to the YAML PIN).
template <typename... Ts>
class SetSecurityPinAction : public Action<Ts...>,
                             public Parented<NukiUartBridgeLock> {
  TEMPLATABLE_VALUE(uint32_t, pin)
public:
  void play(const Ts &...x) override {
    this->parent_->set_security_pin(this->pin_.value(x...));
  }
};

template <typename... Ts>
class VerifyPinAction : public Action<Ts...>,
                        public Parented<NukiUartBridgeLock> {
public:
  void play(const Ts &...x) override { this->parent_->verify_pin(); }
};

template <typename... Ts>
class UpdateTimeAction : public Action<Ts...>,
                         public Parented<NukiUartBridgeLock> {
public:
  void play(const Ts &...x) override { this->parent_->update_time(); }
};

template <typename... Ts>
class RequestCalibrationAction : public Action<Ts...>,
                                 public Parented<NukiUartBridgeLock> {
public:
  void play(const Ts &...x) override { this->parent_->request_calibration(); }
};

template <typename... Ts>
class RebootAction : public Action<Ts...>, public Parented<NukiUartBridgeLock> {
public:
  void play(const Ts &...x) override { this->parent_->request_reboot(); }
};

/// Lock 'n' Go (unlatch: false -> 0x06, true -> 0x08) with an optional
/// name suffix (e.g. the Home Assistant user) for the lock's log entry.
template <typename... Ts>
class LockNGoAction : public Action<Ts...>,
                      public Parented<NukiUartBridgeLock> {
  TEMPLATABLE_VALUE(bool, unlatch)
  TEMPLATABLE_VALUE(std::string, suffix)
public:
  void play(const Ts &...x) override {
    const std::string suffix = this->suffix_.value(x...);
    this->parent_->lock_action(this->unlatch_.value(x...)
                                   ? NUKI_UART_CMD_LOCK_N_GO_UNLATCH
                                   : NUKI_UART_CMD_LOCK_N_GO,
                               suffix.empty() ? nullptr : suffix.c_str());
  }
};

/// Any lock action by UART command byte (0x01 unlock ... 0x0C fob 3).
template <typename... Ts>
class LockActionAction : public Action<Ts...>,
                         public Parented<NukiUartBridgeLock> {
  TEMPLATABLE_VALUE(uint8_t, action)
  TEMPLATABLE_VALUE(std::string, suffix)
public:
  void play(const Ts &...x) override {
    const std::string suffix = this->suffix_.value(x...);
    this->parent_->lock_action(this->action_.value(x...),
                               suffix.empty() ? nullptr : suffix.c_str());
  }
};

// ── Conditions ────────────────────────────────────────────────────────

template <typename... Ts>
class ConnectedCondition : public Condition<Ts...>,
                           public Parented<NukiUartBridgeLock> {
public:
  bool check(const Ts &...x) override { return this->parent_->is_connected(); }
};

template <typename... Ts>
class PairedCondition : public Condition<Ts...>,
                        public Parented<NukiUartBridgeLock> {
public:
  bool check(const Ts &...x) override { return this->parent_->is_paired(); }
};

} // namespace nuki_uart_bridge
} // namespace esphome
