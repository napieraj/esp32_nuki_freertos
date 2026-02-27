#pragma once

#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esphome/core/component.h"
#include "esphome/components/lock/lock.h"
#include "esphome/core/preferences.h"
#include "esphome/core/application.h"

#ifdef USE_API
#include "esphome/components/api/custom_api_device.h"
#endif
#ifdef USE_BUTTON
#include "esphome/components/button/button.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif
#ifdef USE_SELECT
#include "esphome/components/select/select.h"
#endif

#include "NukiLock.h"
#include "NukiConstants.h"
#include "BleScanner.h"

namespace esphome {
namespace nuki_lock {

static const char *TAG = "nuki_lock.lock";

static const uint8_t BLE_CONNECT_TIMEOUT_SEC = 2;
static const uint8_t BLE_CONNECT_RETRIES = 5;
static const uint16_t BLE_DISCONNECT_TIMEOUT = 2000;

static const uint8_t MAX_ACTION_ATTEMPTS = 5;
static const uint8_t MAX_TOLERATED_UPDATES_ERRORS = 5;

static const uint32_t COOLDOWN_COMMANDS_MILLIS = 1000;
static const uint32_t COOLDOWN_COMMANDS_EXTENDED_MILLIS = 3000;
static const uint32_t KEEP_ALIVE_INTERVAL_MILLIS = 10000;

static const uint8_t MAX_AUTH_DATA_ENTRIES = 10;
static const uint8_t MAX_EVENT_LOG_ENTRIES = 3;
static const uint8_t MAX_NAME_LEN = 32;

static const uint16_t BLE_TASK_STACK_SIZE = 12288;
static const uint8_t BLE_TASK_PRIORITY = 5;
static const BaseType_t BLE_TASK_CORE = 0;
static const uint8_t CMD_QUEUE_SIZE = 8;
static const uint8_t RESULT_QUEUE_SIZE = 8;

enum PinState { NotSet = 0, Set = 1, Valid = 2, Invalid = 3 };

struct AuthEntry {
    uint32_t authId;
    char name[MAX_NAME_LEN];
};

struct NukiLockSettings {
    uint32_t security_pin;
    PinState pin_state;
};

// ── Commands: main loop (Core 1) → BLE task (Core 0) ──────────────────

enum class BleCmd : uint8_t {
    LockAction, RequestStatus, RequestConfig, RequestAdvancedConfig,
    RequestAuthData, RequestEventLogs, Pair, Unpair, SetSecurityPin,
    SetConfigSwitch, SetConfigSelect, SetConfigNumber, RequestCalibration,
    PrintKeypadEntries, AddKeypadEntry, UpdateKeypadEntry, DeleteKeypadEntry,
    VerifyPin, SavePin,
};

struct BleCmdMsg {
    BleCmd cmd;
    union {
        NukiLock::LockAction lock_action;
        struct { uint32_t pin; } security_pin;
        struct { char key[40]; bool value; } config_switch;
        struct { char key[40]; char value[50]; } config_select;
        struct { char key[40]; float value; } config_number;
        struct { bool as_app; } pair;
        struct { char name[21]; int32_t code; } add_keypad;
        struct { int32_t id; char name[21]; int32_t code; bool enabled; } update_keypad;
        struct { int32_t id; } delete_keypad;
    };
};

// ── Results: BLE task (Core 0) → main loop (Core 1) ───────────────────

enum class BleResultType : uint8_t {
    StatusUpdate, ConfigUpdate, AdvancedConfigUpdate, ActionResult,
    PairResult, UnpairDone, NotifyEvent, PinVerifyResult, PinSaveResult,
    CommandResult, ConnectedChanged,
};

struct BleResultMsg {
    BleResultType type;
    bool success;

    NukiLock::KeyTurnerState kts;            // StatusUpdate
    bool is_battery_critical;
    uint8_t battery_perc;
    bool battery_charging;
    int rssi;

    // ConfigUpdate
    struct {
        bool has_keypad, has_keypad_v2, pairing_enabled, auto_unlatch;
        bool button_enabled, led_enabled, single_lock, dst_mode;
        uint8_t led_brightness;
        int16_t timezone_offset;
        uint8_t fob_action_1, fob_action_2, fob_action_3;
        Nuki::TimeZoneId timezone_id;
        Nuki::AdvertisingMode advertising_mode;
        bool is_ultra;
    } cfg;

    // AdvancedConfigUpdate
    struct {
        bool night_mode, night_mode_auto_lock, night_mode_auto_unlock_disabled;
        bool night_mode_immediate_lock_on_start;
        bool auto_lock, auto_unlock_disabled, immediate_auto_lock;
        bool auto_update, auto_battery_type_detection;
        bool slow_speed_night_mode, detached_cylinder;
        NukiLock::ButtonPressAction single_button, double_button;
        Nuki::BatteryType battery_type;
        NukiLock::MotorSpeed motor_speed;
        uint8_t lock_n_go_timeout, unlatch_duration;
        uint16_t auto_lock_timeout;
        int16_t unlocked_offset, locked_offset, single_locked_offset, transition_offset;
        bool is_ultra;
    } adv;

    Nuki::EventType event_type;              // NotifyEvent
    bool connected;                          // ConnectedChanged
};

// ── Component class ────────────────────────────────────────────────────

class NukiLockComponent :
    public lock::Lock,
    public PollingComponent,
    public Nuki::SmartlockEventHandler
#ifdef USE_API
    , public api::CustomAPIDevice
#endif
    {
    #ifdef USE_BINARY_SENSOR
    SUB_BINARY_SENSOR(connected)
    SUB_BINARY_SENSOR(paired)
    SUB_BINARY_SENSOR(battery_critical)
    SUB_BINARY_SENSOR(door_sensor)
    #endif
    #ifdef USE_SENSOR
    SUB_SENSOR(battery_level)
    SUB_SENSOR(bt_signal)
    #endif
    #ifdef USE_TEXT_SENSOR
    SUB_TEXT_SENSOR(door_sensor_state)
    SUB_TEXT_SENSOR(last_unlock_user)
    SUB_TEXT_SENSOR(last_lock_action)
    SUB_TEXT_SENSOR(last_lock_action_trigger)
    SUB_TEXT_SENSOR(pin_state)
    #endif
    #ifdef USE_NUMBER
    SUB_NUMBER(led_brightness)
    SUB_NUMBER(timezone_offset)
    SUB_NUMBER(lock_n_go_timeout)
    SUB_NUMBER(auto_lock_timeout)
    SUB_NUMBER(unlatch_duration)
    SUB_NUMBER(unlocked_position_offset)
    SUB_NUMBER(locked_position_offset)
    SUB_NUMBER(single_locked_position_offset)
    SUB_NUMBER(unlocked_to_locked_transition_offset)
    #endif
    #ifdef USE_SELECT
    SUB_SELECT(single_button_press_action)
    SUB_SELECT(double_button_press_action)
    SUB_SELECT(fob_action_1) SUB_SELECT(fob_action_2) SUB_SELECT(fob_action_3)
    SUB_SELECT(timezone) SUB_SELECT(advertising_mode)
    SUB_SELECT(battery_type) SUB_SELECT(motor_speed)
    #endif
    #ifdef USE_BUTTON
    SUB_BUTTON(unpair) SUB_BUTTON(request_calibration)
    #endif
    #ifdef USE_SWITCH
    SUB_SWITCH(pairing_mode) SUB_SWITCH(pairing_enabled)
    SUB_SWITCH(button_enabled) SUB_SWITCH(auto_unlatch_enabled)
    SUB_SWITCH(led_enabled) SUB_SWITCH(nightmode_enabled)
    SUB_SWITCH(night_mode_auto_lock_enabled)
    SUB_SWITCH(night_mode_auto_unlock_disabled)
    SUB_SWITCH(night_mode_immediate_lock_on_start)
    SUB_SWITCH(auto_lock_enabled) SUB_SWITCH(auto_unlock_disabled)
    SUB_SWITCH(immediate_auto_lock_enabled) SUB_SWITCH(auto_update_enabled)
    SUB_SWITCH(single_lock_enabled) SUB_SWITCH(dst_mode_enabled)
    SUB_SWITCH(auto_battery_type_detection_enabled)
    SUB_SWITCH(slow_speed_during_night_mode_enabled)
    SUB_SWITCH(detached_cylinder_enabled)
    #endif

    public:
        const uint32_t deviceId_ = 2020002;
        const std::string deviceName_ = "Nuki ESPHome";

        explicit NukiLockComponent() : Lock(), open_latch_(false),
                                    lock_n_go_(false), keypad_paired_(false),
                                    nuki_lock_(deviceName_, deviceId_) {
            this->traits.set_supports_open(true);
            this->nuki_lock_.setEventHandler(this);
        }

        void setup() override;
        void update() override;
        void dump_config() override;
        float get_setup_priority() const override { return setup_priority::HARDWARE; }

        // SmartlockEventHandler — called from BLE task on Core 0
        void notify(Nuki::EventType event_type) override;

        void set_pairing_mode_timeout(uint32_t v) { pairing_mode_timeout_ = v; }
        void set_query_interval_config(uint32_t v) { query_interval_config_ = v; }
        void set_query_interval_auth_data(uint32_t v) { query_interval_auth_data_ = v; }
        void set_ble_general_timeout(uint32_t v) { ble_general_timeout_ = v; }
        void set_ble_command_timeout(uint32_t v) { ble_command_timeout_ = v; }
        void set_event(const char *event) {
            event_ = event;
            if (strcmp(event, "esphome.none") != 0) send_events_ = true;
        }
        template<typename T> void set_security_pin_config(T v) { security_pin_config_ = v; }
        template<typename T> void set_pairing_as_app(T v) { pairing_as_app_ = v; }

        void add_pairing_mode_on_callback(std::function<void()> &&cb);
        void add_pairing_mode_off_callback(std::function<void()> &&cb);
        void add_paired_callback(std::function<void()> &&cb);
        void add_event_log_received_callback(std::function<void(NukiLock::LogEntry)> &&cb);

        CallbackManager<void()> pairing_mode_on_callback_{};
        CallbackManager<void()> pairing_mode_off_callback_{};
        CallbackManager<void()> paired_callback_{};
        CallbackManager<void(NukiLock::LogEntry)> event_log_received_callback_{};

        lock::LockState nuki_to_lock_state(NukiLock::LockState);
        bool nuki_doorsensor_to_binary(Nuki::DoorSensorState);

        uint8_t fob_action_to_int(const char *str);
        void fob_action_to_string(int action, char* str);
        Nuki::BatteryType battery_type_to_enum(const char* str);
        void battery_type_to_string(Nuki::BatteryType bt, char* str);
        NukiLock::MotorSpeed motor_speed_to_enum(const char* str);
        void motor_speed_to_string(NukiLock::MotorSpeed sp, char* str);
        NukiLock::ButtonPressAction button_press_action_to_enum(const char* str);
        void button_press_action_to_string(NukiLock::ButtonPressAction a, char* str);
        Nuki::TimeZoneId timezone_to_enum(const char *str);
        void timezone_to_string(Nuki::TimeZoneId tz, char* str);
        Nuki::AdvertisingMode advertising_mode_to_enum(const char *str);
        void advertising_mode_to_string(Nuki::AdvertisingMode m, char* str);
        void homekit_status_to_string(int status, char* str);
        void pin_state_to_string(PinState v, char* str);

        void set_security_pin(uint32_t pin);
        void unpair();
        void set_pairing_mode(bool enabled);
        void save_settings();
        void request_calibration();

        bool is_connected() { return connected_; }
        bool is_paired() { return paired_.load(std::memory_order_relaxed); }

        #ifdef USE_NUMBER
        void set_config_number(const char* cfg, float value);
        #endif
        #ifdef USE_SWITCH
        void set_config_switch(const char* cfg, bool value);
        #endif
        #ifdef USE_SELECT
        void set_config_select(const char* cfg, const char* value);
        #endif

    protected:
        void control(const lock::LockCall &call) override;
        void open_latch() override { open_latch_ = true; unlock(); }

        // Queue helpers
        void send_cmd(const BleCmdMsg &msg);
        void drain_results();
        void handle_result(const BleResultMsg &msg);
        void handle_status_result(const BleResultMsg &msg);
        void handle_config_result(const BleResultMsg &msg);
        void handle_advanced_config_result(const BleResultMsg &msg);
        void handle_notify(const BleResultMsg &msg);

        void setup_intervals(bool setup = true);
        void publish_pin_state();
        const char* get_auth_name(uint32_t authId) const;

        // ── BLE task (runs on Core 0) ──────────────────────────────────
        static void ble_task_entry(void *param);
        void ble_task_loop();

        BleScanner::Scanner scanner_;
        NukiLock::KeyTurnerState retrieved_key_turner_state_;
        NukiLock::LockAction lock_action_;

        AuthEntry auth_entries_[MAX_AUTH_DATA_ENTRIES];
        size_t auth_entries_count_ = 0;
        uint32_t auth_id_ = 0;
        char auth_name_[33] = {0};

        uint8_t action_attempts_ = 0;
        uint32_t status_update_consecutive_errors_ = 0;

        std::atomic<bool> status_update_{false};
        std::atomic<bool> config_update_{false};
        std::atomic<bool> advanced_config_update_{false};
        std::atomic<bool> auth_data_update_{false};
        std::atomic<bool> event_log_update_{false};

        bool open_latch_;
        bool lock_n_go_;

        PinState pin_state_ = PinState::NotSet;
        uint32_t security_pin_ = 0;
        TemplatableValue<uint32_t> security_pin_config_{};
        TemplatableValue<bool> pairing_as_app_{};

        bool connected_ = false;
        std::atomic<bool> paired_{false};
        std::atomic<bool> pairing_mode_{false};

        const char* event_;
        bool send_events_ = false;

        uint32_t query_interval_auth_data_ = 0;
        uint32_t query_interval_config_ = 0;
        uint32_t ble_general_timeout_ = 0;
        uint32_t ble_command_timeout_ = 0;
        uint32_t pairing_mode_timeout_ = 0;

        uint32_t last_rolling_log_id = 0;

        ESPPreferenceObject pref_;

        // FreeRTOS IPC
        QueueHandle_t cmd_queue_{nullptr};
        QueueHandle_t result_queue_{nullptr};
        TaskHandle_t ble_task_handle_{nullptr};

    private:
        NukiLock::NukiLock nuki_lock_;

        void lock_n_go();
        void print_keypad_entries();
        void add_keypad_entry(std::string name, int32_t code);
        void update_keypad_entry(int32_t id, std::string name, int32_t code, bool enabled);
        void delete_keypad_entry(int32_t id);
        bool valid_keypad_id(int32_t id);
        bool valid_keypad_name(std::string name);
        bool valid_keypad_code(int32_t code);

        std::vector<uint16_t> keypad_code_ids_;
        bool keypad_paired_;
};

// ── Entity sub-classes (identical interface to original) ───────────────

#ifdef USE_BUTTON
class NukiLockUnpairButton : public button::Button, public Parented<NukiLockComponent> {
    public: NukiLockUnpairButton() = default;
    protected: void press_action() override;
};
class NukiLockRequestCalibrationButton : public button::Button, public Parented<NukiLockComponent> {
    public: NukiLockRequestCalibrationButton() = default;
    protected: void press_action() override;
};
#endif

#ifdef USE_SELECT
class NukiLockSingleButtonPressActionSelect : public select::Select, public Parented<NukiLockComponent> {
    public: NukiLockSingleButtonPressActionSelect() = default;
    protected: void control(const std::string &v) override;
};
class NukiLockDoubleButtonPressActionSelect : public select::Select, public Parented<NukiLockComponent> {
    public: NukiLockDoubleButtonPressActionSelect() = default;
    protected: void control(const std::string &v) override;
};
class NukiLockFobAction1Select : public select::Select, public Parented<NukiLockComponent> {
    public: NukiLockFobAction1Select() = default;
    protected: void control(const std::string &v) override;
};
class NukiLockFobAction2Select : public select::Select, public Parented<NukiLockComponent> {
    public: NukiLockFobAction2Select() = default;
    protected: void control(const std::string &v) override;
};
class NukiLockFobAction3Select : public select::Select, public Parented<NukiLockComponent> {
    public: NukiLockFobAction3Select() = default;
    protected: void control(const std::string &v) override;
};
class NukiLockTimeZoneSelect : public select::Select, public Parented<NukiLockComponent> {
    public: NukiLockTimeZoneSelect() = default;
    protected: void control(const std::string &v) override;
};
class NukiLockAdvertisingModeSelect : public select::Select, public Parented<NukiLockComponent> {
    public: NukiLockAdvertisingModeSelect() = default;
    protected: void control(const std::string &v) override;
};
class NukiLockBatteryTypeSelect : public select::Select, public Parented<NukiLockComponent> {
    public: NukiLockBatteryTypeSelect() = default;
    protected: void control(const std::string &v) override;
};
class NukiLockMotorSpeedSelect : public select::Select, public Parented<NukiLockComponent> {
    public: NukiLockMotorSpeedSelect() = default;
    protected: void control(const std::string &v) override;
};
#endif

#ifdef USE_SWITCH
class NukiLockPairingModeSwitch : public switch_::Switch, public Parented<NukiLockComponent> {
    public: NukiLockPairingModeSwitch() = default;
    protected: void write_state(bool state) override;
};
class NukiLockPairingEnabledSwitch : public switch_::Switch, public Parented<NukiLockComponent> {
    public: NukiLockPairingEnabledSwitch() = default;
    protected: void write_state(bool state) override;
};
class NukiLockAutoUnlatchEnabledSwitch : public switch_::Switch, public Parented<NukiLockComponent> {
    public: NukiLockAutoUnlatchEnabledSwitch() = default;
    protected: void write_state(bool state) override;
};
class NukiLockButtonEnabledSwitch : public switch_::Switch, public Parented<NukiLockComponent> {
    public: NukiLockButtonEnabledSwitch() = default;
    protected: void write_state(bool state) override;
};
class NukiLockLedEnabledSwitch : public switch_::Switch, public Parented<NukiLockComponent> {
    public: NukiLockLedEnabledSwitch() = default;
    protected: void write_state(bool state) override;
};
class NukiLockNightModeEnabledSwitch : public switch_::Switch, public Parented<NukiLockComponent> {
    public: NukiLockNightModeEnabledSwitch() = default;
    protected: void write_state(bool state) override;
};
class NukiLockNightModeAutoLockEnabledSwitch : public switch_::Switch, public Parented<NukiLockComponent> {
    public: NukiLockNightModeAutoLockEnabledSwitch() = default;
    protected: void write_state(bool state) override;
};
class NukiLockNightModeAutoUnlockDisabledSwitch : public switch_::Switch, public Parented<NukiLockComponent> {
    public: NukiLockNightModeAutoUnlockDisabledSwitch() = default;
    protected: void write_state(bool state) override;
};
class NukiLockNightModeImmediateLockOnStartEnabledSwitch : public switch_::Switch, public Parented<NukiLockComponent> {
    public: NukiLockNightModeImmediateLockOnStartEnabledSwitch() = default;
    protected: void write_state(bool state) override;
};
class NukiLockAutoLockEnabledSwitch : public switch_::Switch, public Parented<NukiLockComponent> {
    public: NukiLockAutoLockEnabledSwitch() = default;
    protected: void write_state(bool state) override;
};
class NukiLockAutoUnlockDisabledSwitch : public switch_::Switch, public Parented<NukiLockComponent> {
    public: NukiLockAutoUnlockDisabledSwitch() = default;
    protected: void write_state(bool state) override;
};
class NukiLockImmediateAutoLockEnabledSwitch : public switch_::Switch, public Parented<NukiLockComponent> {
    public: NukiLockImmediateAutoLockEnabledSwitch() = default;
    protected: void write_state(bool state) override;
};
class NukiLockAutoUpdateEnabledSwitch : public switch_::Switch, public Parented<NukiLockComponent> {
    public: NukiLockAutoUpdateEnabledSwitch() = default;
    protected: void write_state(bool state) override;
};
class NukiLockSingleLockEnabledSwitch : public switch_::Switch, public Parented<NukiLockComponent> {
    public: NukiLockSingleLockEnabledSwitch() = default;
    protected: void write_state(bool state) override;
};
class NukiLockDstModeEnabledSwitch : public switch_::Switch, public Parented<NukiLockComponent> {
    public: NukiLockDstModeEnabledSwitch() = default;
    protected: void write_state(bool state) override;
};
class NukiLockAutoBatteryTypeDetectionEnabledSwitch : public switch_::Switch, public Parented<NukiLockComponent> {
    public: NukiLockAutoBatteryTypeDetectionEnabledSwitch() = default;
    protected: void write_state(bool state) override;
};
class NukiLockSlowSpeedDuringNightModeEnabledSwitch : public switch_::Switch, public Parented<NukiLockComponent> {
    public: NukiLockSlowSpeedDuringNightModeEnabledSwitch() = default;
    protected: void write_state(bool state) override;
};
class NukiLockDetachedCylinderEnabledSwitch : public switch_::Switch, public Parented<NukiLockComponent> {
    public: NukiLockDetachedCylinderEnabledSwitch() = default;
    protected: void write_state(bool state) override;
};
#endif

#ifdef USE_NUMBER
class NukiLockLedBrightnessNumber : public number::Number, public Parented<NukiLockComponent> {
    public: NukiLockLedBrightnessNumber() = default;
    protected: void control(float value) override;
};
class NukiLockTimeZoneOffsetNumber : public number::Number, public Parented<NukiLockComponent> {
    public: NukiLockTimeZoneOffsetNumber() = default;
    protected: void control(float value) override;
};
class NukiLockLockNGoTimeoutNumber : public number::Number, public Parented<NukiLockComponent> {
    public: NukiLockLockNGoTimeoutNumber() = default;
    protected: void control(float value) override;
};
class NukiLockAutoLockTimeoutNumber : public number::Number, public Parented<NukiLockComponent> {
    public: NukiLockAutoLockTimeoutNumber() = default;
    protected: void control(float value) override;
};
class NukiLockUnlatchDurationNumber : public number::Number, public Parented<NukiLockComponent> {
    public: NukiLockUnlatchDurationNumber() = default;
    protected: void control(float value) override;
};
class NukiLockUnlockedPositionOffsetDegreesNumber : public number::Number, public Parented<NukiLockComponent> {
    public: NukiLockUnlockedPositionOffsetDegreesNumber() = default;
    protected: void control(float value) override;
};
class NukiLockLockedPositionOffsetDegreesNumber : public number::Number, public Parented<NukiLockComponent> {
    public: NukiLockLockedPositionOffsetDegreesNumber() = default;
    protected: void control(float value) override;
};
class NukiLockSingleLockedPositionOffsetDegreesNumber : public number::Number, public Parented<NukiLockComponent> {
    public: NukiLockSingleLockedPositionOffsetDegreesNumber() = default;
    protected: void control(float value) override;
};
class NukiLockUnlockedToLockedTransitionOffsetDegreesNumber : public number::Number, public Parented<NukiLockComponent> {
    public: NukiLockUnlockedToLockedTransitionOffsetDegreesNumber() = default;
    protected: void control(float value) override;
};
#endif

} //namespace nuki_lock
} //namespace esphome
