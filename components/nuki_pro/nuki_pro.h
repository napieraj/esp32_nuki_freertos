#pragma once

#include "esphome/core/component.h"
#include "esphome/components/lock/lock.h"
#include "esphome/core/preferences.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <atomic>
#include <memory>
#include <string>

#include "NukiLock.h"
#include "NukiConstants.h"
#include "BleScanner.h"

namespace esphome {
namespace nuki_pro {

static const char *const TAG = "nuki_pro.lock";

enum class NukiCommandType : uint8_t {
    LOCK, UNLOCK, UNLATCH, LOCK_N_GO, PAIR, UNPAIR, NONE
};

struct NukiCommand {
    NukiCommandType type;
};

static const uint8_t BLE_CONNECT_TIMEOUT = 2;
static const uint8_t BLE_CONNECT_RETRIES = 5;
static const uint16_t BLE_DISCONNECT_TIMEOUT = 2000;
static const uint8_t MAX_ACTION_ATTEMPTS = 5;

class NukiProLock : public lock::Lock, public Component,
                    public Nuki::SmartlockEventHandler {
 public:
    NukiProLock() {
        this->traits.set_supports_open(true);
    }

    void set_pin(uint32_t pin) { this->pin_ = pin; }
    void set_device_id(uint32_t id) { this->device_id_ = id; }
    void set_poll_interval(uint32_t ms) { this->poll_interval_ms_ = ms; }
    void set_keepalive(bool v) { this->keepalive_ = v; }

    void setup() override;
    void loop() override;
    void dump_config() override;
    float get_setup_priority() const override { return setup_priority::HARDWARE; }

    void notify(Nuki::EventType event_type) override;

 protected:
    void control(const lock::LockCall &call) override;
    void open_latch() override;

    static void nuki_task_runner(void *arg);
    void nuki_task();

    lock::LockState nuki_to_esphome_state(NukiLock::LockState nuki_state);
    bool execute_lock_action(NukiLock::LockAction action);
    void do_status_poll();
    void do_pair();
    void save_pairing_data();
    void load_pairing_data();

    uint32_t pin_{0};
    uint32_t device_id_{2020002};
    uint32_t poll_interval_ms_{100};
    bool keepalive_{true};

    TaskHandle_t task_handle_{nullptr};
    QueueHandle_t command_queue_{nullptr};

    std::atomic<lock::LockState> pending_state_{lock::LOCK_STATE_NONE};
    std::atomic<bool> state_updated_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> paired_{false};
    std::atomic<bool> status_poll_requested_{true};
    std::atomic<bool> pairing_mode_{false};

    BleScanner::Scanner scanner_;
    std::unique_ptr<NukiLock::NukiLock> nuki_lock_;
    NukiLock::KeyTurnerState key_turner_state_;

    ESPPreferenceObject paired_pref_;
};

}  // namespace nuki_pro
}  // namespace esphome
