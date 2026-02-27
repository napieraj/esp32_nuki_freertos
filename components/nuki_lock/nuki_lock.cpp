#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/core/preferences.h"

#ifdef USE_API
#include "esphome/components/api/custom_api_device.h"
#endif

#include <map>
#include <cstring>
#include <esp_task_wdt.h>

#include "nuki_lock.h"

namespace esphome {
namespace nuki_lock {

uint32_t global_nuki_lock_id = 1912044075ULL;

// ═══════════════════════════════════════════════════════════════════════════
//  Conversion functions (ported verbatim from original)
// ═══════════════════════════════════════════════════════════════════════════

lock::LockState NukiLockComponent::nuki_to_lock_state(NukiLock::LockState nukiLockState) {
    switch(nukiLockState) {
        case NukiLock::LockState::Locked:
            return lock::LOCK_STATE_LOCKED;
        case NukiLock::LockState::Unlocked:
        case NukiLock::LockState::Unlatched:
            return lock::LOCK_STATE_UNLOCKED;
        case NukiLock::LockState::MotorBlocked:
            return lock::LOCK_STATE_JAMMED;
        case NukiLock::LockState::Locking:
            return lock::LOCK_STATE_LOCKING;
        case NukiLock::LockState::Unlocking:
        case NukiLock::LockState::Unlatching:
            return lock::LOCK_STATE_UNLOCKING;
        default:
            return lock::LOCK_STATE_NONE;
    }
}

bool NukiLockComponent::nuki_doorsensor_to_binary(Nuki::DoorSensorState nuki_door_sensor_state) {
    if (nuki_door_sensor_state == Nuki::DoorSensorState::DoorClosed) {
        return false;
    }
    return true;
}

NukiLock::ButtonPressAction NukiLockComponent::button_press_action_to_enum(const char* str)
{
    if (strcmp(str, "No action") == 0) {
        return NukiLock::ButtonPressAction::NoAction;
    } else if (strcmp(str, "Intelligent") == 0) {
        return NukiLock::ButtonPressAction::Intelligent;
    } else if (strcmp(str, "Unlock") == 0) {
        return NukiLock::ButtonPressAction::Unlock;
    } else if (strcmp(str, "Lock") == 0) {
        return NukiLock::ButtonPressAction::Lock;
    } else if (strcmp(str, "Open door") == 0) {
        return NukiLock::ButtonPressAction::Unlatch;
    } else if (strcmp(str, "Lock 'n' Go") == 0) {
        return NukiLock::ButtonPressAction::LockNgo;
    } else if (strcmp(str, "Show state") == 0) {
        return NukiLock::ButtonPressAction::ShowStatus;
    }
    return NukiLock::ButtonPressAction::NoAction;
}

void NukiLockComponent::button_press_action_to_string(NukiLock::ButtonPressAction action, char* str) {
    switch (action) {
        case NukiLock::ButtonPressAction::NoAction:
            strcpy(str, "No action");
            break;
        case NukiLock::ButtonPressAction::Intelligent:
            strcpy(str, "Intelligent");
            break;
        case NukiLock::ButtonPressAction::Unlock:
            strcpy(str, "Unlock");
            break;
        case NukiLock::ButtonPressAction::Lock:
            strcpy(str, "Lock");
            break;
        case NukiLock::ButtonPressAction::Unlatch:
            strcpy(str, "Open door");
            break;
        case NukiLock::ButtonPressAction::LockNgo:
            strcpy(str, "Lock 'n' Go");
            break;
        case NukiLock::ButtonPressAction::ShowStatus:
            strcpy(str, "Show state");
            break;
        default:
            strcpy(str, "No action");
            break;
    }
}

void NukiLockComponent::battery_type_to_string(Nuki::BatteryType battery_type, char* str) {
    switch (battery_type) {
        case Nuki::BatteryType::Alkali:
            strcpy(str, "Alkali");
            break;
        case Nuki::BatteryType::Accumulators:
            strcpy(str, "Accumulators");
            break;
        case Nuki::BatteryType::Lithium:
            strcpy(str, "Lithium");
            break;
        default:
            strcpy(str, "undefined");
            break;
    }
}

Nuki::BatteryType NukiLockComponent::battery_type_to_enum(const char* str) {
    if(strcmp(str, "Alkali") == 0) {
        return Nuki::BatteryType::Alkali;
    } else if(strcmp(str, "Accumulators") == 0) {
        return Nuki::BatteryType::Accumulators;
    } else if(strcmp(str, "Lithium") == 0) {
        return Nuki::BatteryType::Lithium;
    }
    return (Nuki::BatteryType)0xff;
}

void NukiLockComponent::homekit_status_to_string(int status, char* str) {
    switch (status) {
        case 0:
            strcpy(str, "Not Available");
            break;
        case 1:
            strcpy(str, "Disabled");
            break;
        case 2:
            strcpy(str, "Enabled");
            break;
        case 3:
            strcpy(str, "Enabled & Paired");
            break;
        default:
            strcpy(str, "undefined");
            break;
    }
}

void NukiLockComponent::motor_speed_to_string(NukiLock::MotorSpeed speed, char* str) {
    switch (speed) {
        case NukiLock::MotorSpeed::Standard:
            strcpy(str, "Standard");
            break;
        case NukiLock::MotorSpeed::Insane:
            strcpy(str, "Insane");
            break;
        case NukiLock::MotorSpeed::Gentle:
            strcpy(str, "Gentle");
            break;
        default:
            strcpy(str, "undefined");
            break;
    }
}

NukiLock::MotorSpeed NukiLockComponent::motor_speed_to_enum(const char* str) {
    if(strcmp(str, "Standard") == 0) {
        return NukiLock::MotorSpeed::Standard;
    } else if(strcmp(str, "Insane") == 0) {
        return NukiLock::MotorSpeed::Insane;
    } else if(strcmp(str, "Gentle") == 0) {
        return NukiLock::MotorSpeed::Gentle;
    }
    return NukiLock::MotorSpeed::Standard;
}

uint8_t NukiLockComponent::fob_action_to_int(const char *str) {
    if(strcmp(str, "No action") == 0) {
        return 0;
    } else if(strcmp(str, "Unlock") == 0) {
        return 1;
    } else if(strcmp(str, "Lock") == 0) {
        return 2;
    } else if(strcmp(str, "Lock 'n' Go") == 0) {
        return 3;
    } else if(strcmp(str, "Intelligent") == 0) {
        return 4;
    }
    return 99;
}

void NukiLockComponent::fob_action_to_string(int action, char* str) {
    switch (action) {
        case 0:
            strcpy(str, "No action");
            break;
        case 1:
            strcpy(str, "Unlock");
            break;
        case 2:
            strcpy(str, "Lock");
            break;
        case 3:
            strcpy(str, "Lock 'n' Go");
            break;
        case 4:
            strcpy(str, "Intelligent");
            break;
        default:
            strcpy(str, "No action");
            break;
    }
}

Nuki::TimeZoneId NukiLockComponent::timezone_to_enum(const char *str) {
    if(strcmp(str, "Africa/Cairo") == 0) {
        return Nuki::TimeZoneId::Africa_Cairo;
    } else if(strcmp(str, "Africa/Lagos") == 0) {
        return Nuki::TimeZoneId::Africa_Lagos;
    } else if(strcmp(str, "Africa/Maputo") == 0) {
        return Nuki::TimeZoneId::Africa_Maputo;
    } else if(strcmp(str, "Africa/Nairobi") == 0) {
        return Nuki::TimeZoneId::Africa_Nairobi;
    } else if(strcmp(str, "America/Anchorage") == 0) {
        return Nuki::TimeZoneId::America_Anchorage;
    } else if(strcmp(str, "America/Argentina/Buenos_Aires") == 0) {
        return Nuki::TimeZoneId::America_Argentina_Buenos_Aires;
    } else if(strcmp(str, "America/Chicago") == 0) {
        return Nuki::TimeZoneId::America_Chicago;
    } else if(strcmp(str, "America/Denver") == 0) {
        return Nuki::TimeZoneId::America_Denver;
    } else if(strcmp(str, "America/Halifax") == 0) {
        return Nuki::TimeZoneId::America_Halifax;
    } else if(strcmp(str, "America/Los_Angeles") == 0) {
        return Nuki::TimeZoneId::America_Los_Angeles;
    } else if(strcmp(str, "America/Manaus") == 0) {
        return Nuki::TimeZoneId::America_Manaus;
    } else if(strcmp(str, "America/Mexico_City") == 0) {
        return Nuki::TimeZoneId::America_Mexico_City;
    } else if(strcmp(str, "America/New_York") == 0) {
        return Nuki::TimeZoneId::America_New_York;
    } else if(strcmp(str, "America/Phoenix") == 0) {
        return Nuki::TimeZoneId::America_Phoenix;
    } else if(strcmp(str, "America/Regina") == 0) {
        return Nuki::TimeZoneId::America_Regina;
    } else if(strcmp(str, "America/Santiago") == 0) {
        return Nuki::TimeZoneId::America_Santiago;
    } else if(strcmp(str, "America/Sao_Paulo") == 0) {
        return Nuki::TimeZoneId::America_Sao_Paulo;
    } else if(strcmp(str, "America/St_Johns") == 0) {
        return Nuki::TimeZoneId::America_St_Johns;
    } else if(strcmp(str, "Asia/Bangkok") == 0) {
        return Nuki::TimeZoneId::Asia_Bangkok;
    } else if(strcmp(str, "Asia/Dubai") == 0) {
        return Nuki::TimeZoneId::Asia_Dubai;
    } else if(strcmp(str, "Asia/Hong_Kong") == 0) {
        return Nuki::TimeZoneId::Asia_Hong_Kong;
    } else if(strcmp(str, "Asia/Jerusalem") == 0) {
        return Nuki::TimeZoneId::Asia_Jerusalem;
    } else if(strcmp(str, "Asia/Karachi") == 0) {
        return Nuki::TimeZoneId::Asia_Karachi;
    } else if(strcmp(str, "Asia/Kathmandu") == 0) {
        return Nuki::TimeZoneId::Asia_Kathmandu;
    } else if(strcmp(str, "Asia/Kolkata") == 0) {
        return Nuki::TimeZoneId::Asia_Kolkata;
    } else if(strcmp(str, "Asia/Riyadh") == 0) {
        return Nuki::TimeZoneId::Asia_Riyadh;
    } else if(strcmp(str, "Asia/Seoul") == 0) {
        return Nuki::TimeZoneId::Asia_Seoul;
    } else if(strcmp(str, "Asia/Shanghai") == 0) {
        return Nuki::TimeZoneId::Asia_Shanghai;
    } else if(strcmp(str, "Asia/Tehran") == 0) {
        return Nuki::TimeZoneId::Asia_Tehran;
    } else if(strcmp(str, "Asia/Tokyo") == 0) {
        return Nuki::TimeZoneId::Asia_Tokyo;
    } else if(strcmp(str, "Asia/Yangon") == 0) {
        return Nuki::TimeZoneId::Asia_Yangon;
    } else if(strcmp(str, "Australia/Adelaide") == 0) {
        return Nuki::TimeZoneId::Australia_Adelaide;
    } else if(strcmp(str, "Australia/Brisbane") == 0) {
        return Nuki::TimeZoneId::Australia_Brisbane;
    } else if(strcmp(str, "Australia/Darwin") == 0) {
        return Nuki::TimeZoneId::Australia_Darwin;
    } else if(strcmp(str, "Australia/Hobart") == 0) {
        return Nuki::TimeZoneId::Australia_Hobart;
    } else if(strcmp(str, "Australia/Perth") == 0) {
        return Nuki::TimeZoneId::Australia_Perth;
    } else if(strcmp(str, "Australia/Sydney") == 0) {
        return Nuki::TimeZoneId::Australia_Sydney;
    } else if(strcmp(str, "Europe/Berlin") == 0) {
        return Nuki::TimeZoneId::Europe_Berlin;
    } else if(strcmp(str, "Europe/Helsinki") == 0) {
        return Nuki::TimeZoneId::Europe_Helsinki;
    } else if(strcmp(str, "Europe/Istanbul") == 0) {
        return Nuki::TimeZoneId::Europe_Istanbul;
    } else if(strcmp(str, "Europe/London") == 0) {
        return Nuki::TimeZoneId::Europe_London;
    } else if(strcmp(str, "Europe/Moscow") == 0) {
        return Nuki::TimeZoneId::Europe_Moscow;
    } else if(strcmp(str, "Pacific/Auckland") == 0) {
        return Nuki::TimeZoneId::Pacific_Auckland;
    } else if(strcmp(str, "Pacific/Guam") == 0) {
        return Nuki::TimeZoneId::Pacific_Guam;
    } else if(strcmp(str, "Pacific/Honolulu") == 0) {
        return Nuki::TimeZoneId::Pacific_Honolulu;
    } else if(strcmp(str, "Pacific/Pago_Pago") == 0) {
        return Nuki::TimeZoneId::Pacific_Pago_Pago;
    } else if(strcmp(str, "None") == 0) {
        return Nuki::TimeZoneId::None;
    }
    return (Nuki::TimeZoneId)0xff;
}

void NukiLockComponent::timezone_to_string(Nuki::TimeZoneId timeZoneId, char* str) {
    switch (timeZoneId) {
        case Nuki::TimeZoneId::Africa_Cairo:
            strcpy(str, "Africa/Cairo");
            break;
        case Nuki::TimeZoneId::Africa_Lagos:
            strcpy(str, "Africa/Lagos");
            break;
        case Nuki::TimeZoneId::Africa_Maputo:
            strcpy(str, "Africa/Maputo");
            break;
        case Nuki::TimeZoneId::Africa_Nairobi:
            strcpy(str, "Africa/Nairobi");
            break;
        case Nuki::TimeZoneId::America_Anchorage:
            strcpy(str, "America/Anchorage");
            break;
        case Nuki::TimeZoneId::America_Argentina_Buenos_Aires:
            strcpy(str, "America/Argentina/Buenos_Aires");
            break;
        case Nuki::TimeZoneId::America_Chicago:
            strcpy(str, "America/Chicago");
            break;
        case Nuki::TimeZoneId::America_Denver:
            strcpy(str, "America/Denver");
            break;
        case Nuki::TimeZoneId::America_Halifax:
            strcpy(str, "America/Halifax");
            break;
        case Nuki::TimeZoneId::America_Los_Angeles:
            strcpy(str, "America/Los_Angeles");
            break;
        case Nuki::TimeZoneId::America_Manaus:
            strcpy(str, "America/Manaus");
            break;
        case Nuki::TimeZoneId::America_Mexico_City:
            strcpy(str, "America/Mexico_City");
            break;
        case Nuki::TimeZoneId::America_New_York:
            strcpy(str, "America/New_York");
            break;
        case Nuki::TimeZoneId::America_Phoenix:
            strcpy(str, "America/Phoenix");
            break;
        case Nuki::TimeZoneId::America_Regina:
            strcpy(str, "America/Regina");
            break;
        case Nuki::TimeZoneId::America_Santiago:
            strcpy(str, "America/Santiago");
            break;
        case Nuki::TimeZoneId::America_Sao_Paulo:
            strcpy(str, "America/Sao_Paulo");
            break;
        case Nuki::TimeZoneId::America_St_Johns:
            strcpy(str, "America/St_Johns");
            break;
        case Nuki::TimeZoneId::Asia_Bangkok:
            strcpy(str, "Asia/Bangkok");
            break;
        case Nuki::TimeZoneId::Asia_Dubai:
            strcpy(str, "Asia/Dubai");
            break;
        case Nuki::TimeZoneId::Asia_Hong_Kong:
            strcpy(str, "Asia/Hong_Kong");
            break;
        case Nuki::TimeZoneId::Asia_Jerusalem:
            strcpy(str, "Asia/Jerusalem");
            break;
        case Nuki::TimeZoneId::Asia_Karachi:
            strcpy(str, "Asia/Karachi");
            break;
        case Nuki::TimeZoneId::Asia_Kathmandu:
            strcpy(str, "Asia/Kathmandu");
            break;
        case Nuki::TimeZoneId::Asia_Kolkata:
            strcpy(str, "Asia/Kolkata");
            break;
        case Nuki::TimeZoneId::Asia_Riyadh:
            strcpy(str, "Asia/Riyadh");
            break;
        case Nuki::TimeZoneId::Asia_Seoul:
            strcpy(str, "Asia/Seoul");
            break;
        case Nuki::TimeZoneId::Asia_Shanghai:
            strcpy(str, "Asia/Shanghai");
            break;
        case Nuki::TimeZoneId::Asia_Tehran:
            strcpy(str, "Asia/Tehran");
            break;
        case Nuki::TimeZoneId::Asia_Tokyo:
            strcpy(str, "Asia/Tokyo");
            break;
        case Nuki::TimeZoneId::Asia_Yangon:
            strcpy(str, "Asia/Yangon");
            break;
        case Nuki::TimeZoneId::Australia_Adelaide:
            strcpy(str, "Australia/Adelaide");
            break;
        case Nuki::TimeZoneId::Australia_Brisbane:
            strcpy(str, "Australia/Brisbane");
            break;
        case Nuki::TimeZoneId::Australia_Darwin:
            strcpy(str, "Australia/Darwin");
            break;
        case Nuki::TimeZoneId::Australia_Hobart:
            strcpy(str, "Australia/Hobart");
            break;
        case Nuki::TimeZoneId::Australia_Perth:
            strcpy(str, "Australia/Perth");
            break;
        case Nuki::TimeZoneId::Australia_Sydney:
            strcpy(str, "Australia/Sydney");
            break;
        case Nuki::TimeZoneId::Europe_Berlin:
            strcpy(str, "Europe/Berlin");
            break;
        case Nuki::TimeZoneId::Europe_Helsinki:
            strcpy(str, "Europe/Helsinki");
            break;
        case Nuki::TimeZoneId::Europe_Istanbul:
            strcpy(str, "Europe/Istanbul");
            break;
        case Nuki::TimeZoneId::Europe_London:
            strcpy(str, "Europe/London");
            break;
        case Nuki::TimeZoneId::Europe_Moscow:
            strcpy(str, "Europe/Moscow");
            break;
        case Nuki::TimeZoneId::Pacific_Auckland:
            strcpy(str, "Pacific/Auckland");
            break;
        case Nuki::TimeZoneId::Pacific_Guam:
            strcpy(str, "Pacific/Guam");
            break;
        case Nuki::TimeZoneId::Pacific_Honolulu:
            strcpy(str, "Pacific/Honolulu");
            break;
        case Nuki::TimeZoneId::Pacific_Pago_Pago:
            strcpy(str, "Pacific/Pago_Pago");
            break;
        case Nuki::TimeZoneId::None:
            strcpy(str, "None");
            break;
        default:
            strcpy(str, "None");
            break;
    }
}

Nuki::AdvertisingMode NukiLockComponent::advertising_mode_to_enum(const char *str) {
    if(strcmp(str, "Automatic") == 0) {
        return Nuki::AdvertisingMode::Automatic;
    } else if(strcmp(str, "Normal") == 0) {
        return Nuki::AdvertisingMode::Normal;
    } else if(strcmp(str, "Slow") == 0) {
        return Nuki::AdvertisingMode::Slow;
    } else if(strcmp(str, "Slowest") == 0) {
        return Nuki::AdvertisingMode::Slowest;
    }
    return (Nuki::AdvertisingMode)0xff;
}

void NukiLockComponent::advertising_mode_to_string(Nuki::AdvertisingMode mode, char* str) {
    switch (mode) {
        case Nuki::AdvertisingMode::Automatic:
            strcpy(str, "Automatic");
            break;
        case Nuki::AdvertisingMode::Normal:
            strcpy(str, "Normal");
            break;
        case Nuki::AdvertisingMode::Slow:
            strcpy(str, "Slow");
            break;
        case Nuki::AdvertisingMode::Slowest:
            strcpy(str, "Slowest");
            break;
        default:
            strcpy(str, "Normal");
            break;
    }
}

void NukiLockComponent::pin_state_to_string(PinState value, char* str)
{
    switch(value)
    {
        case PinState::NotSet:
            strcpy(str, "Not set");
            break;
        case PinState::Set:
            strcpy(str, "Validation pending");
            break;
        case PinState::Valid:
            strcpy(str, "Valid");
            break;
        case PinState::Invalid:
            strcpy(str, "Invalid");
            break;
        default:
            strcpy(str, "Unknown");
            break;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Utility functions
// ═══════════════════════════════════════════════════════════════════════════

void NukiLockComponent::save_settings() {
    NukiLockSettings settings {
        this->security_pin_,
        this->pin_state_
    };

    if (!this->pref_.save(&settings)) {
        ESP_LOGW(TAG, "Failed to save settings");
    }
}

void NukiLockComponent::publish_pin_state() {
    #ifdef USE_TEXT_SENSOR
    char pin_state_as_string[20] = {0};
    this->pin_state_to_string(this->pin_state_, pin_state_as_string);

    if (this->pin_state_text_sensor_ != nullptr && this->pin_state_text_sensor_->state != pin_state_as_string) {
        this->pin_state_text_sensor_->publish_state(pin_state_as_string);
    }
    #endif
}

const char* NukiLockComponent::get_auth_name(uint32_t authId) const {
    for (size_t i = 0; i < auth_entries_count_; i++) {
        if (auth_entries_[i].authId == authId) {
            return auth_entries_[i].name;
        }
    }
    return nullptr;
}

void NukiLockComponent::setup_intervals(bool setup) {
    this->cancel_interval("update_config");
    this->cancel_interval("update_auth_data");

    if(setup) {
        this->set_interval("update_config", this->query_interval_config_ * 1000, [this]() {
            this->config_update_.store(true);
            this->advanced_config_update_.store(true);
        });

        this->set_interval("update_auth_data", this->query_interval_auth_data_ * 1000, [this]() {
            this->auth_data_update_.store(true);
        });
    }
}

bool NukiLockComponent::valid_keypad_id(int32_t id) {
    bool is_valid = std::find(keypad_code_ids_.begin(), keypad_code_ids_.end(), id) != keypad_code_ids_.end();
    if (!is_valid) {
        ESP_LOGE(TAG, "Keypad id %d unknown.", id);
    }
    return is_valid;
}

bool NukiLockComponent::valid_keypad_name(std::string name) {
    bool name_valid = !(name == "" || name == "--");
    if (!name_valid) {
        ESP_LOGE(TAG, "Keypad name '%s' is invalid.", name.c_str());
    }
    return name_valid;
}

bool NukiLockComponent::valid_keypad_code(int32_t code) {
    bool code_valid = (code > 100000 && code < 1000000 && (std::to_string(code).find('0') == std::string::npos));
    if (!code_valid) {
        ESP_LOGE(TAG, "Keypad code %d is invalid. Code must be 6 digits, without 0.", code);
    }
    return code_valid;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Queue helpers
// ═══════════════════════════════════════════════════════════════════════════

void NukiLockComponent::send_cmd(const BleCmdMsg &msg) {
    if (cmd_queue_ && xQueueSend(cmd_queue_, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to enqueue BLE command %d", (int)msg.cmd);
    }
}

void NukiLockComponent::drain_results() {
    BleResultMsg msg;
    while (result_queue_ && xQueueReceive(result_queue_, &msg, 0) == pdTRUE) {
        handle_result(msg);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Result handlers (run on Core 1 — main loop)
// ═══════════════════════════════════════════════════════════════════════════

void NukiLockComponent::handle_result(const BleResultMsg &msg) {
    switch (msg.type) {
        case BleResultType::StatusUpdate:
            handle_status_result(msg);
            break;

        case BleResultType::ConfigUpdate:
            handle_config_result(msg);
            break;

        case BleResultType::AdvancedConfigUpdate:
            handle_advanced_config_result(msg);
            break;

        case BleResultType::ActionResult: {
            if (msg.success) {
                ESP_LOGI(TAG, "Lock action succeeded");
                action_attempts_ = 0;
            } else {
                action_attempts_--;
                if (action_attempts_ > 0) {
                    ESP_LOGW(TAG, "Lock action failed, retrying (%d attempts left)", action_attempts_);
                    BleCmdMsg cmd{};
                    cmd.cmd = BleCmd::LockAction;
                    cmd.lock_action = lock_action_;
                    send_cmd(cmd);
                } else {
                    ESP_LOGE(TAG, "Lock action failed after all attempts");
                    connected_ = false;
                    publish_state(lock::LOCK_STATE_NONE);
                    #ifdef USE_BINARY_SENSOR
                    if (connected_binary_sensor_ != nullptr) {
                        connected_binary_sensor_->publish_state(false);
                    }
                    #endif
                }
            }
            status_update_.store(true);
            break;
        }

        case BleResultType::PairResult: {
            if (msg.success) {
                const char* pairing_type = pairing_as_app_.value_or(false) ? "App" : "Bridge";
                const char* lock_type = msg.cfg.is_ultra ? "Ultra / Go / 5th Gen" : "1st - 4th Gen";
                ESP_LOGI(TAG, "Successfully paired as %s with a %s smart lock!", pairing_type, lock_type);

                paired_callback_.call();
                set_pairing_mode(false);

                const uint32_t pin_to_use = security_pin_ != 0 ? security_pin_ : security_pin_config_.value_or(0);
                bool is_ultra = msg.cfg.is_ultra;

                if (security_pin_ != 0) {
                    ESP_LOGW(TAG, "Using security pin override instead of YAML config");
                }

                if (pin_to_use == 0) {
                    ESP_LOGD(TAG, "No security pin configured, skipping pin setup");
                    pin_state_ = PinState::NotSet;
                    save_settings();
                    publish_pin_state();
                } else if (pin_to_use > 999999) {
                    ESP_LOGE(TAG, "Invalid security pin detected! Maximum is 6 digits (999999)");
                    pin_state_ = PinState::Invalid;
                    save_settings();
                    publish_pin_state();
                } else if (!is_ultra && pin_to_use > 65535) {
                    ESP_LOGE(TAG, "Security pin exceeds maximum of 65535 for 1st-4th gen locks");
                    pin_state_ = PinState::Invalid;
                    save_settings();
                    publish_pin_state();
                } else {
                    pin_state_ = PinState::Set;
                    save_settings();
                    publish_pin_state();

                    BleCmdMsg cmd{};
                    cmd.cmd = BleCmd::SetSecurityPin;
                    cmd.security_pin.pin = pin_to_use;
                    send_cmd(cmd);
                }

                setup_intervals();
            }

            #ifdef USE_BINARY_SENSOR
            if (paired_binary_sensor_ != nullptr) {
                paired_binary_sensor_->publish_state(msg.success);
            }
            #endif
            break;
        }

        case BleResultType::UnpairDone: {
            connected_ = false;
            publish_state(lock::LOCK_STATE_NONE);

            security_pin_ = 0;
            if (security_pin_ == 0 && security_pin_config_.value_or(0) == 0) {
                pin_state_ = PinState::NotSet;
                ESP_LOGD(TAG, "The security pin is now unset!");
            } else {
                pin_state_ = PinState::Set;
            }
            publish_pin_state();
            save_settings();

            setup_intervals(false);

            #ifdef USE_BINARY_SENSOR
            if (paired_binary_sensor_ != nullptr) {
                paired_binary_sensor_->publish_state(false);
            }
            if (connected_binary_sensor_ != nullptr) {
                connected_binary_sensor_->publish_state(false);
            }
            #endif

            ESP_LOGI(TAG, "Unpaired Nuki! Turn on Pairing Mode to pair a new Nuki.");
            break;
        }

        case BleResultType::NotifyEvent:
            handle_notify(msg);
            break;

        case BleResultType::PinVerifyResult:
            if (msg.success) {
                ESP_LOGI(TAG, "Nuki Lock PIN is valid");
                if (pin_state_ != PinState::Valid) {
                    pin_state_ = PinState::Valid;
                    save_settings();
                    publish_pin_state();
                }
            } else {
                ESP_LOGW(TAG, "Nuki Lock PIN is invalid");
                if (pin_state_ != PinState::Invalid) {
                    pin_state_ = PinState::Invalid;
                    save_settings();
                    publish_pin_state();
                }
            }
            break;

        case BleResultType::PinSaveResult:
            if (msg.success) {
                ESP_LOGI(TAG, "Successfully saved security pin");
                BleCmdMsg cmd{};
                cmd.cmd = BleCmd::VerifyPin;
                send_cmd(cmd);
            } else {
                ESP_LOGE(TAG, "Failed to save security pin");
                pin_state_ = PinState::Invalid;
                save_settings();
                publish_pin_state();
            }
            break;

        case BleResultType::CommandResult:
            if (!msg.success) {
                ESP_LOGW(TAG, "BLE command failed");
            }
            #ifdef USE_TEXT_SENSOR
            if (last_unlock_user_text_sensor_ != nullptr && strlen(auth_name_) > 0) {
                if (last_unlock_user_text_sensor_->state != auth_name_) {
                    last_unlock_user_text_sensor_->publish_state(auth_name_);
                }
            }
            #endif
            break;

        case BleResultType::ConnectedChanged:
            connected_ = msg.connected;
            #ifdef USE_BINARY_SENSOR
            if (connected_binary_sensor_ != nullptr) {
                connected_binary_sensor_->publish_state(connected_);
            }
            #endif
            break;
    }
}

void NukiLockComponent::handle_status_result(const BleResultMsg &msg) {
    char str[50] = {0};

    if (msg.success) {
        status_update_consecutive_errors_ = 0;
        connected_ = true;

        NukiLock::LockState current_lock_state = msg.kts.lockState;
        char current_lock_state_as_string[30] = {0};
        NukiLock::lockstateToString(current_lock_state, current_lock_state_as_string);

        ESP_LOGI(TAG, "Lock state: %s (%d), Battery (critical: %d, level: %d, charging: %s)",
            current_lock_state_as_string,
            current_lock_state,
            msg.is_battery_critical,
            msg.battery_perc,
            YESNO(msg.battery_charging)
        );

        this->publish_state(nuki_to_lock_state(msg.kts.lockState));

        #ifdef USE_BINARY_SENSOR
        if (this->connected_binary_sensor_ != nullptr) {
            this->connected_binary_sensor_->publish_state(true);
        }

        if (this->battery_critical_binary_sensor_ != nullptr) {
            this->battery_critical_binary_sensor_->publish_state(msg.is_battery_critical);
        }

        if (this->pin_state_ == PinState::Set) {
            BleCmdMsg cmd{};
            cmd.cmd = BleCmd::VerifyPin;
            send_cmd(cmd);
        }

        if (this->door_sensor_binary_sensor_ != nullptr) {
            Nuki::DoorSensorState door_sensor_state = msg.kts.doorSensorState;
            if (door_sensor_state != Nuki::DoorSensorState::Unavailable) {
                this->door_sensor_binary_sensor_->publish_state(this->nuki_doorsensor_to_binary(door_sensor_state));
            } else {
                this->door_sensor_binary_sensor_->invalidate_state();
            }
        }
        #endif

        #ifdef USE_SENSOR
        if (this->battery_level_sensor_ != nullptr) {
            this->battery_level_sensor_->publish_state(msg.battery_perc);
        }
        if (this->bt_signal_sensor_ != nullptr) {
            this->bt_signal_sensor_->publish_state(msg.rssi);
        }
        #endif

        #ifdef USE_TEXT_SENSOR
        if (this->door_sensor_state_text_sensor_ != nullptr) {
            memset(str, 0, sizeof(str));
            NukiLock::doorSensorStateToString(msg.kts.doorSensorState, str);
            this->door_sensor_state_text_sensor_->publish_state(str);
        }

        if (this->last_lock_action_text_sensor_ != nullptr) {
            memset(str, 0, sizeof(str));
            NukiLock::lockactionToString(msg.kts.lastLockAction, str);
            this->last_lock_action_text_sensor_->publish_state(str);
        }

        if (this->last_lock_action_trigger_text_sensor_ != nullptr) {
            memset(str, 0, sizeof(str));
            NukiLock::triggerToString(msg.kts.lastLockActionTrigger, str);
            this->last_lock_action_trigger_text_sensor_->publish_state(str);
        }

        if (this->last_unlock_user_text_sensor_ != nullptr && msg.kts.lastLockActionTrigger == NukiLock::Trigger::Manual) {
            this->last_unlock_user_text_sensor_->publish_state("Manual");
        }
        #endif

        if (msg.kts.lockState == NukiLock::LockState::Locking ||
            msg.kts.lockState == NukiLock::LockState::Unlocking) {
            status_update_.store(true);
            if (this->send_events_) {
                event_log_update_.store(true);
            }
        }
    } else {
        ESP_LOGE(TAG, "Status update failed");
        status_update_.store(true);
        status_update_consecutive_errors_++;

        if (status_update_consecutive_errors_ > MAX_TOLERATED_UPDATES_ERRORS) {
            connected_ = false;
            publish_state(lock::LOCK_STATE_NONE);

            #ifdef USE_BINARY_SENSOR
            if (this->connected_binary_sensor_ != nullptr) {
                this->connected_binary_sensor_->publish_state(false);
            }
            #endif
        }
    }
}

void NukiLockComponent::handle_config_result(const BleResultMsg &msg) {
    if (!msg.success) {
        ESP_LOGE(TAG, "Config update failed");
        config_update_.store(true);
        return;
    }

    keypad_paired_ = msg.cfg.has_keypad || msg.cfg.has_keypad_v2;

    char str[50] = {0};

    #ifdef USE_SWITCH
    if (this->pairing_enabled_switch_ != nullptr) {
        this->pairing_enabled_switch_->publish_state(msg.cfg.pairing_enabled);
    }
    if (this->auto_unlatch_enabled_switch_ != nullptr) {
        this->auto_unlatch_enabled_switch_->publish_state(msg.cfg.auto_unlatch);
    }
    if (this->button_enabled_switch_ != nullptr) {
        this->button_enabled_switch_->publish_state(msg.cfg.button_enabled);
    }
    if (this->led_enabled_switch_ != nullptr) {
        this->led_enabled_switch_->publish_state(msg.cfg.led_enabled);
    }
    if (this->single_lock_enabled_switch_ != nullptr) {
        this->single_lock_enabled_switch_->publish_state(msg.cfg.single_lock);
    }
    if (this->dst_mode_enabled_switch_ != nullptr) {
        this->dst_mode_enabled_switch_->publish_state(msg.cfg.dst_mode);
    }
    #endif

    #ifdef USE_NUMBER
    if (this->led_brightness_number_ != nullptr) {
        this->led_brightness_number_->publish_state(msg.cfg.led_brightness);
    }
    if (this->timezone_offset_number_ != nullptr) {
        this->timezone_offset_number_->publish_state(msg.cfg.timezone_offset);
    }
    #endif

    #ifdef USE_SELECT
    if (this->fob_action_1_select_ != nullptr) {
        memset(str, 0, sizeof(str));
        this->fob_action_to_string(msg.cfg.fob_action_1, str);
        this->fob_action_1_select_->publish_state(str);
    }
    if (this->fob_action_2_select_ != nullptr) {
        memset(str, 0, sizeof(str));
        this->fob_action_to_string(msg.cfg.fob_action_2, str);
        this->fob_action_2_select_->publish_state(str);
    }
    if (this->fob_action_3_select_ != nullptr) {
        memset(str, 0, sizeof(str));
        this->fob_action_to_string(msg.cfg.fob_action_3, str);
        this->fob_action_3_select_->publish_state(str);
    }
    if (this->timezone_select_ != nullptr) {
        memset(str, 0, sizeof(str));
        this->timezone_to_string(msg.cfg.timezone_id, str);
        this->timezone_select_->publish_state(str);
    }
    if (this->advertising_mode_select_ != nullptr) {
        memset(str, 0, sizeof(str));
        this->advertising_mode_to_string(msg.cfg.advertising_mode, str);
        this->advertising_mode_select_->publish_state(str);
    }
    #endif
}

void NukiLockComponent::handle_advanced_config_result(const BleResultMsg &msg) {
    if (!msg.success) {
        ESP_LOGE(TAG, "Advanced config update failed");
        advanced_config_update_.store(true);
        return;
    }

    char str[50] = {0};

    #ifdef USE_SWITCH
    if (this->nightmode_enabled_switch_ != nullptr) {
        this->nightmode_enabled_switch_->publish_state(msg.adv.night_mode);
    }
    if (this->night_mode_auto_lock_enabled_switch_ != nullptr) {
        this->night_mode_auto_lock_enabled_switch_->publish_state(msg.adv.night_mode_auto_lock);
    }
    if (this->night_mode_auto_unlock_disabled_switch_ != nullptr) {
        this->night_mode_auto_unlock_disabled_switch_->publish_state(msg.adv.night_mode_auto_unlock_disabled);
    }
    if (this->night_mode_immediate_lock_on_start_switch_ != nullptr) {
        this->night_mode_immediate_lock_on_start_switch_->publish_state(msg.adv.night_mode_immediate_lock_on_start);
    }
    if (this->auto_lock_enabled_switch_ != nullptr) {
        this->auto_lock_enabled_switch_->publish_state(msg.adv.auto_lock);
    }
    if (this->auto_unlock_disabled_switch_ != nullptr) {
        this->auto_unlock_disabled_switch_->publish_state(msg.adv.auto_unlock_disabled);
    }
    if (this->immediate_auto_lock_enabled_switch_ != nullptr) {
        this->immediate_auto_lock_enabled_switch_->publish_state(msg.adv.immediate_auto_lock);
    }
    if (this->auto_update_enabled_switch_ != nullptr) {
        this->auto_update_enabled_switch_->publish_state(msg.adv.auto_update);
    }
    if (!msg.adv.is_ultra && this->auto_battery_type_detection_enabled_switch_ != nullptr) {
        this->auto_battery_type_detection_enabled_switch_->publish_state(msg.adv.auto_battery_type_detection);
    }
    if (msg.adv.is_ultra && this->slow_speed_during_night_mode_enabled_switch_ != nullptr) {
        this->slow_speed_during_night_mode_enabled_switch_->publish_state(msg.adv.slow_speed_night_mode);
    }
    if (this->detached_cylinder_enabled_switch_ != nullptr) {
        this->detached_cylinder_enabled_switch_->publish_state(msg.adv.detached_cylinder);
    }
    #endif

    #ifdef USE_NUMBER
    if (this->lock_n_go_timeout_number_ != nullptr) {
        this->lock_n_go_timeout_number_->publish_state(msg.adv.lock_n_go_timeout);
    }
    if (this->auto_lock_timeout_number_ != nullptr) {
        this->auto_lock_timeout_number_->publish_state(msg.adv.auto_lock_timeout);
    }
    if (this->unlatch_duration_number_ != nullptr) {
        this->unlatch_duration_number_->publish_state(msg.adv.unlatch_duration);
    }
    if (this->unlocked_position_offset_number_ != nullptr) {
        this->unlocked_position_offset_number_->publish_state(msg.adv.unlocked_offset);
    }
    if (this->locked_position_offset_number_ != nullptr) {
        this->locked_position_offset_number_->publish_state(msg.adv.locked_offset);
    }
    if (this->single_locked_position_offset_number_ != nullptr) {
        this->single_locked_position_offset_number_->publish_state(msg.adv.single_locked_offset);
    }
    if (this->unlocked_to_locked_transition_offset_number_ != nullptr) {
        this->unlocked_to_locked_transition_offset_number_->publish_state(msg.adv.transition_offset);
    }
    #endif

    #ifdef USE_SELECT
    if (this->single_button_press_action_select_ != nullptr) {
        memset(str, 0, sizeof(str));
        this->button_press_action_to_string(msg.adv.single_button, str);
        this->single_button_press_action_select_->publish_state(str);
    }
    if (this->double_button_press_action_select_ != nullptr) {
        memset(str, 0, sizeof(str));
        this->button_press_action_to_string(msg.adv.double_button, str);
        this->double_button_press_action_select_->publish_state(str);
    }
    if (!msg.adv.is_ultra && this->battery_type_select_ != nullptr) {
        memset(str, 0, sizeof(str));
        this->battery_type_to_string(msg.adv.battery_type, str);
        this->battery_type_select_->publish_state(str);
    }
    if (msg.adv.is_ultra && this->motor_speed_select_ != nullptr) {
        memset(str, 0, sizeof(str));
        this->motor_speed_to_string(msg.adv.motor_speed, str);
        this->motor_speed_select_->publish_state(str);
    }
    #endif
}

void NukiLockComponent::handle_notify(const BleResultMsg &msg) {
    ESP_LOGI(TAG, "Processing event notification %d", msg.event_type);

    if (msg.event_type == Nuki::EventType::KeyTurnerStatusReset) {
        ESP_LOGD(TAG, "KeyTurnerStatusReset");
    } else if (msg.event_type == Nuki::EventType::ERROR_BAD_PIN) {
        ESP_LOGW(TAG, "Nuki reported an invalid security PIN");

        ESP_LOGD(TAG, "ESPHome PIN (override): %d", this->security_pin_);
        ESP_LOGD(TAG, "ESPHome PIN (YAML): %d", this->security_pin_config_.value_or(0));

        this->pin_state_ = PinState::Invalid;
        this->save_settings();
        this->publish_pin_state();
    } else if (msg.event_type == Nuki::EventType::KeyTurnerStatusUpdated) {
        ESP_LOGD(TAG, "KeyTurnerStatusUpdated");
        this->status_update_.store(true);
    } else if (msg.event_type == Nuki::EventType::BLE_ERROR_ON_DISCONNECT) {
        ESP_LOGE(TAG, "Failed to disconnect from Nuki. Restarting ESP...");
        delay(100);  // NOLINT
        App.safe_reboot();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  setup() — runs on Core 1
// ═══════════════════════════════════════════════════════════════════════════

void NukiLockComponent::setup() {
    ESP_LOGCONFIG(TAG, "Running setup");

    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 15000,
        .trigger_panic = false
    };
    esp_task_wdt_reconfigure(&wdt_config);

    this->pref_ = global_preferences->make_preference<NukiLockSettings>(global_nuki_lock_id);

    NukiLockSettings recovered;
    if (!this->pref_.load(&recovered)) {
        recovered = {0};
    }

    this->pin_state_ = recovered.pin_state;
    this->security_pin_ = recovered.security_pin;

    this->traits.set_supported_states({
        lock::LOCK_STATE_NONE,
        lock::LOCK_STATE_LOCKED,
        lock::LOCK_STATE_UNLOCKED,
        lock::LOCK_STATE_JAMMED,
        lock::LOCK_STATE_LOCKING,
        lock::LOCK_STATE_UNLOCKING
    });

    this->scanner_.initialize("ESPHomeNuki", true, 40, 40);
    this->scanner_.setScanDuration(0);

    App.feed_wdt();

    ESP_LOGD(TAG, "Prepare security pin for initialization");

    if (this->security_pin_ > 999999) {
        ESP_LOGE(TAG, "Invalid security pin (override) detected! The pin can't be longer than 6 digits. Unset override pin.");
        this->security_pin_ = 0;
        this->pin_state_ = PinState::Invalid;
        this->save_settings();
    }

    uint32_t pin_to_use = 0;
    if (this->security_pin_ != 0) {
        ESP_LOGW(TAG, "Note: Using security pin override, not yaml config pin!");
        ESP_LOGD(TAG, "Security pin: %u (override)", this->security_pin_);
        pin_to_use = this->security_pin_;
    } else if(this->security_pin_config_.value_or(0) != 0) {
        ESP_LOGD(TAG, "Security pin: %u (yaml config)", this->security_pin_config_.value_or(0));
        pin_to_use = this->security_pin_config_.value_or(0);
    } else {
        this->pin_state_ = PinState::NotSet;
        ESP_LOGW(TAG, "The security pin is not set. The security pin is crucial to pair a Smart Lock Ultra.");
    }

    if (pin_to_use != 0) {
        if (pin_to_use > 999999) {
            ESP_LOGE(TAG, "Invalid security pin detected! The pin can't be longer than 6 digits.");
            this->pin_state_ = PinState::Invalid;
            this->save_settings();
        } else {
            ESP_LOGD(TAG, "Set security pin before init: %u", pin_to_use);
            this->nuki_lock_.saveUltraPincode((unsigned int)pin_to_use, false);
        }
    }

    this->nuki_lock_.setDebugConnect(true);
    this->nuki_lock_.setDebugCommunication(false);
    this->nuki_lock_.setDebugReadableData(false);
    this->nuki_lock_.setDebugHexData(false);
    this->nuki_lock_.setDebugCommand(false);

    this->nuki_lock_.initialize();
    this->nuki_lock_.registerBleScanner(&this->scanner_);
    this->nuki_lock_.setConnectTimeout(BLE_CONNECT_TIMEOUT_SEC);
    this->nuki_lock_.setConnectRetries(BLE_CONNECT_RETRIES);
    this->nuki_lock_.setDisconnectTimeout(BLE_DISCONNECT_TIMEOUT);
    this->nuki_lock_.setGeneralTimeout(this->ble_general_timeout_ * 1000);
    this->nuki_lock_.setCommandTimeout(this->ble_command_timeout_ * 1000);

    App.feed_wdt();

    // Create FreeRTOS queues
    this->cmd_queue_ = xQueueCreate(CMD_QUEUE_SIZE, sizeof(BleCmdMsg));
    this->result_queue_ = xQueueCreate(RESULT_QUEUE_SIZE, sizeof(BleResultMsg));

    if (this->nuki_lock_.isPairedWithLock()) {
        this->paired_.store(true);
        this->status_update_.store(true);
        this->config_update_.store(true);
        this->advanced_config_update_.store(true);

        if (this->send_events_) {
            this->auth_data_update_.store(true);
            this->event_log_update_.store(true);
        }

        const char* pairing_type = this->pairing_as_app_.value_or(false) ? "App" : "Bridge";
        const char* lock_type = this->nuki_lock_.isLockUltra() ? "Ultra / Go / 5th Gen" : "1st - 4th Gen";
        ESP_LOGI(TAG, "This component is already paired as %s with a %s smart lock!", pairing_type, lock_type);

        #ifdef USE_BINARY_SENSOR
        if (this->paired_binary_sensor_ != nullptr) {
            this->paired_binary_sensor_->publish_initial_state(true);
        }
        #endif

        this->setup_intervals();
    } else {
        ESP_LOGI(TAG, "This component is not paired yet. Enable the pairing mode to pair with your smart lock.");
        #ifdef USE_BINARY_SENSOR
        if (this->paired_binary_sensor_ != nullptr) {
            this->paired_binary_sensor_->publish_initial_state(false);
        }
        #endif
    }

    // Spawn the BLE task on Core 0 (after all nuki_lock_ init is complete)
    xTaskCreatePinnedToCore(
        ble_task_entry, "nukiBLE",
        BLE_TASK_STACK_SIZE, this,
        BLE_TASK_PRIORITY, &ble_task_handle_,
        BLE_TASK_CORE
    );

    this->publish_pin_state();
    this->publish_state(lock::LOCK_STATE_NONE);

    #ifdef USE_API
        #ifdef USE_API_CUSTOM_SERVICES
        this->register_service(&NukiLockComponent::lock_n_go, "lock_n_go");
        this->register_service(&NukiLockComponent::print_keypad_entries, "print_keypad_entries");
        this->register_service(&NukiLockComponent::add_keypad_entry, "add_keypad_entry", {"name", "code"});
        this->register_service(&NukiLockComponent::update_keypad_entry, "update_keypad_entry", {"id", "name", "code", "enabled"});
        this->register_service(&NukiLockComponent::delete_keypad_entry, "delete_keypad_entry", {"id"});
        #else
        ESP_LOGW(TAG, "CUSTOM API SERVICES ARE DISABLED");
        ESP_LOGW(TAG, "Please set 'api:' -> 'custom_services: true' to use API services.");
        ESP_LOGW(TAG, "More information here: https://esphome.io/components/api.html");
        #endif

        #ifndef USE_API_HOMEASSISTANT_SERVICES
        ESP_LOGW(TAG, "NUKI EVENT LOGS ARE DISABLED");
        ESP_LOGW(TAG, "Please set 'api:' -> 'homeassistant_services: true' to fire Home Assistant events.");
        ESP_LOGW(TAG, "More information here: https://esphome.io/components/api.html");
        #endif
    #endif
}

// ═══════════════════════════════════════════════════════════════════════════
//  update() — runs on Core 1 (every 500ms)
// ═══════════════════════════════════════════════════════════════════════════

void NukiLockComponent::update() {
    drain_results();
}

// ═══════════════════════════════════════════════════════════════════════════
//  BLE task — runs on Core 0
// ═══════════════════════════════════════════════════════════════════════════

void NukiLockComponent::ble_task_entry(void *param) {
    static_cast<NukiLockComponent*>(param)->ble_task_loop();
}

void NukiLockComponent::ble_task_loop() {
    // ── Helper lambdas to avoid duplicating request logic ──────────────

    auto do_status_request = [this]() -> BleResultMsg {
        NukiLock::KeyTurnerState kts;
        Nuki::CmdResult result = nuki_lock_.requestKeyTurnerState(&kts);
        esp_task_wdt_reset();

        BleResultMsg rmsg{};
        rmsg.type = BleResultType::StatusUpdate;
        rmsg.success = (result == Nuki::CmdResult::Success);
        if (rmsg.success) {
            rmsg.kts = kts;
            rmsg.is_battery_critical = nuki_lock_.isBatteryCritical();
            rmsg.battery_perc = nuki_lock_.getBatteryPerc();
            rmsg.battery_charging = nuki_lock_.isBatteryCharging();
            rmsg.rssi = nuki_lock_.getRssi();
        }
        return rmsg;
    };

    auto do_config_request = [this]() -> BleResultMsg {
        NukiLock::Config config;
        Nuki::CmdResult result = nuki_lock_.requestConfig(&config);
        esp_task_wdt_reset();

        BleResultMsg rmsg{};
        rmsg.type = BleResultType::ConfigUpdate;
        rmsg.success = (result == Nuki::CmdResult::Success);
        if (rmsg.success) {
            rmsg.cfg.has_keypad = config.hasKeypad;
            rmsg.cfg.has_keypad_v2 = config.hasKeypadV2;
            rmsg.cfg.pairing_enabled = config.pairingEnabled;
            rmsg.cfg.auto_unlatch = config.autoUnlatch;
            rmsg.cfg.button_enabled = config.buttonEnabled;
            rmsg.cfg.led_enabled = config.ledEnabled;
            rmsg.cfg.single_lock = config.singleLock;
            rmsg.cfg.dst_mode = config.dstMode;
            rmsg.cfg.led_brightness = config.ledBrightness;
            rmsg.cfg.timezone_offset = config.timeZoneOffset;
            rmsg.cfg.fob_action_1 = config.fobAction1;
            rmsg.cfg.fob_action_2 = config.fobAction2;
            rmsg.cfg.fob_action_3 = config.fobAction3;
            rmsg.cfg.timezone_id = config.timeZoneId;
            rmsg.cfg.advertising_mode = config.advertisingMode;
            rmsg.cfg.is_ultra = nuki_lock_.isLockUltra();
        }
        return rmsg;
    };

    auto do_advanced_config_request = [this]() -> BleResultMsg {
        NukiLock::AdvancedConfig adv;
        Nuki::CmdResult result = nuki_lock_.requestAdvancedConfig(&adv);
        esp_task_wdt_reset();

        BleResultMsg rmsg{};
        rmsg.type = BleResultType::AdvancedConfigUpdate;
        rmsg.success = (result == Nuki::CmdResult::Success);
        if (rmsg.success) {
            rmsg.adv.night_mode = adv.nightModeEnabled;
            rmsg.adv.night_mode_auto_lock = adv.nightModeAutoLockEnabled;
            rmsg.adv.night_mode_auto_unlock_disabled = adv.nightModeAutoUnlockDisabled;
            rmsg.adv.night_mode_immediate_lock_on_start = adv.nightModeImmediateLockOnStart;
            rmsg.adv.auto_lock = adv.autoLockEnabled;
            rmsg.adv.auto_unlock_disabled = adv.autoUnLockDisabled;
            rmsg.adv.immediate_auto_lock = adv.immediateAutoLockEnabled;
            rmsg.adv.auto_update = adv.autoUpdateEnabled;
            rmsg.adv.auto_battery_type_detection = adv.automaticBatteryTypeDetection;
            rmsg.adv.slow_speed_night_mode = adv.enableSlowSpeedDuringNightMode;
            rmsg.adv.detached_cylinder = adv.detachedCylinder;
            rmsg.adv.single_button = adv.singleButtonPressAction;
            rmsg.adv.double_button = adv.doubleButtonPressAction;
            rmsg.adv.battery_type = adv.batteryType;
            rmsg.adv.motor_speed = adv.motorSpeed;
            rmsg.adv.lock_n_go_timeout = adv.lockNgoTimeout;
            rmsg.adv.unlatch_duration = adv.unlatchDuration;
            rmsg.adv.auto_lock_timeout = adv.autoLockTimeOut;
            rmsg.adv.unlocked_offset = adv.unlockedPositionOffsetDegrees;
            rmsg.adv.locked_offset = adv.lockedPositionOffsetDegrees;
            rmsg.adv.single_locked_offset = adv.singleLockedPositionOffsetDegrees;
            rmsg.adv.transition_offset = adv.unlockedToLockedTransitionOffsetDegrees;
            rmsg.adv.is_ultra = nuki_lock_.isLockUltra();
        }
        return rmsg;
    };

    auto do_auth_data_request = [this]() {
        if (pin_state_ != PinState::Valid) {
            ESP_LOGW(TAG, "Cannot retrieve auth data: PIN not valid");
            BleResultMsg rmsg{};
            rmsg.type = BleResultType::CommandResult;
            rmsg.success = false;
            xQueueSend(result_queue_, &rmsg, portMAX_DELAY);
            return;
        }

        Nuki::CmdResult result = nuki_lock_.retrieveAuthorizationEntries(0, MAX_AUTH_DATA_ENTRIES);
        esp_task_wdt_reset();

        if (result == Nuki::CmdResult::Success) {
            std::list<NukiLock::AuthorizationEntry> authEntries;
            nuki_lock_.getAuthorizationEntries(&authEntries);

            if (!authEntries.empty()) {
                ESP_LOGD(TAG, "Authorization Entry Count: %d", authEntries.size());

                authEntries.sort([](const NukiLock::AuthorizationEntry& a, const NukiLock::AuthorizationEntry& b) {
                    return a.authId < b.authId;
                });

                if (authEntries.size() > MAX_AUTH_DATA_ENTRIES) {
                    authEntries.resize(MAX_AUTH_DATA_ENTRIES);
                }

                auth_entries_count_ = 0;
                for (const auto& entry : authEntries) {
                    if (auth_entries_count_ >= MAX_AUTH_DATA_ENTRIES) break;
                    AuthEntry& ae = auth_entries_[auth_entries_count_];
                    ae.authId = entry.authId;
                    strncpy(ae.name, reinterpret_cast<const char*>(entry.name), MAX_NAME_LEN - 1);
                    ae.name[MAX_NAME_LEN - 1] = '\0';
                    ESP_LOGD(TAG, "Authorization entry[%d] type: %d name: %s", entry.authId, entry.idType, entry.name);
                    auth_entries_count_++;
                }
            } else {
                ESP_LOGW(TAG, "No auth entries!");
            }
        }

        BleResultMsg rmsg{};
        rmsg.type = BleResultType::CommandResult;
        rmsg.success = (result == Nuki::CmdResult::Success);
        xQueueSend(result_queue_, &rmsg, portMAX_DELAY);
    };

    auto do_event_log_request = [this]() {
        if (pin_state_ != PinState::Valid) {
            ESP_LOGW(TAG, "Cannot retrieve event logs: PIN not valid");
            BleResultMsg rmsg{};
            rmsg.type = BleResultType::CommandResult;
            rmsg.success = false;
            xQueueSend(result_queue_, &rmsg, portMAX_DELAY);
            return;
        }

        Nuki::CmdResult result = nuki_lock_.retrieveLogEntries(0, MAX_EVENT_LOG_ENTRIES, 1, false);
        esp_task_wdt_reset();

        if (result == Nuki::CmdResult::Success) {
            std::list<NukiLock::LogEntry> log;
            nuki_lock_.getLogEntries(&log);

            if (!log.empty()) {
                ESP_LOGD(TAG, "Log Entry Count: %d", log.size());

                log.sort([](const NukiLock::LogEntry& a, const NukiLock::LogEntry& b) {
                    return a.index < b.index;
                });

                if (log.size() > MAX_EVENT_LOG_ENTRIES) {
                    log.resize(MAX_EVENT_LOG_ENTRIES);
                }

                char buffer[50] = {0};
                uint32_t auth_index = 0;

                for (const auto& entry : log) {
                    if (entry.loggingType == NukiLock::LoggingType::LockAction ||
                        entry.loggingType == NukiLock::LoggingType::KeypadAction) {

                        strncpy(buffer, reinterpret_cast<const char*>(entry.name), sizeof(buffer) - 1);
                        buffer[sizeof(buffer) - 1] = '\0';

                        if (strcmp(buffer, "") == 0) {
                            strcpy(buffer, "Manual");
                        }

                        if (entry.index > auth_index) {
                            auth_index = entry.index;
                            auth_id_ = entry.authId;

                            strncpy(auth_name_, buffer, sizeof(auth_name_) - 1);
                            auth_name_[sizeof(auth_name_) - 1] = '\0';

                            const char* authNameFromEntries = get_auth_name(auth_id_);
                            if (authNameFromEntries) {
                                strncpy(auth_name_, authNameFromEntries, sizeof(auth_name_) - 1);
                                auth_name_[sizeof(auth_name_) - 1] = '\0';
                            }
                        }
                    }

                    if (entry.index > last_rolling_log_id) {
                        last_rolling_log_id = entry.index;
                        event_log_received_callback_.call(entry);
                    }
                }
            } else {
                ESP_LOGW(TAG, "No log entries!");
            }
        }

        BleResultMsg rmsg{};
        rmsg.type = BleResultType::CommandResult;
        rmsg.success = (result == Nuki::CmdResult::Success);
        xQueueSend(result_queue_, &rmsg, portMAX_DELAY);
    };

    // ── Helper: execute a config-switch command on nuki_lock_ ──────────

    auto exec_config_switch = [this](const char* config, bool value) -> Nuki::CmdResult {
        Nuki::CmdResult cmd_result = (Nuki::CmdResult)-1;
        bool is_advanced = false;

        if (strcmp(config, "pairing_enabled") == 0) {
            cmd_result = nuki_lock_.enablePairing(value);
        } else if (strcmp(config, "auto_unlatch_enabled") == 0) {
            cmd_result = nuki_lock_.enableAutoUnlatch(value);
        } else if (strcmp(config, "button_enabled") == 0) {
            cmd_result = nuki_lock_.enableButton(value);
        } else if (strcmp(config, "led_enabled") == 0) {
            cmd_result = nuki_lock_.enableLedFlash(value);
        } else if (strcmp(config, "nightmode_enabled") == 0) {
            cmd_result = nuki_lock_.enableNightMode(value);
            is_advanced = true;
        } else if (strcmp(config, "night_mode_auto_lock_enabled") == 0) {
            cmd_result = nuki_lock_.enableNightModeAutoLock(value);
            is_advanced = true;
        } else if (strcmp(config, "night_mode_auto_unlock_disabled") == 0) {
            cmd_result = nuki_lock_.disableNightModeAutoUnlock(value);
            is_advanced = true;
        } else if (strcmp(config, "night_mode_immediate_lock_on_start") == 0) {
            cmd_result = nuki_lock_.enableNightModeImmediateLockOnStart(value);
            is_advanced = true;
        } else if (strcmp(config, "auto_lock_enabled") == 0) {
            cmd_result = nuki_lock_.enableAutoLock(value);
            is_advanced = true;
        } else if (strcmp(config, "auto_unlock_disabled") == 0) {
            cmd_result = nuki_lock_.disableAutoUnlock(value);
            is_advanced = true;
        } else if (strcmp(config, "immediate_auto_lock_enabled") == 0) {
            cmd_result = nuki_lock_.enableImmediateAutoLock(value);
            is_advanced = true;
        } else if (strcmp(config, "auto_update_enabled") == 0) {
            cmd_result = nuki_lock_.enableAutoUpdate(value);
            is_advanced = true;
        } else if (strcmp(config, "single_lock_enabled") == 0) {
            cmd_result = nuki_lock_.enableSingleLock(value);
        } else if (strcmp(config, "dst_mode_enabled") == 0) {
            cmd_result = nuki_lock_.enableDst(value);
        } else if (!nuki_lock_.isLockUltra() && strcmp(config, "auto_battery_type_detection_enabled") == 0) {
            cmd_result = nuki_lock_.enableAutoBatteryTypeDetection(value);
        } else if (nuki_lock_.isLockUltra() && strcmp(config, "slow_speed_during_night_mode_enabled") == 0) {
            cmd_result = nuki_lock_.enableSlowSpeedDuringNightMode(value);
        } else if (strcmp(config, "detached_cylinder_enabled") == 0) {
            cmd_result = nuki_lock_.enableDetachedCylinder(value);
        }

        esp_task_wdt_reset();

        if (cmd_result == Nuki::CmdResult::Success) {
            if (is_advanced) advanced_config_update_.store(true);
            else config_update_.store(true);
        } else {
            ESP_LOGE(TAG, "Saving setting %s failed (result %d)", config, cmd_result);
        }
        return cmd_result;
    };

    // ── Helper: execute a config-select command on nuki_lock_ ──────────

    auto exec_config_select = [this](const char* config, const char* value) -> Nuki::CmdResult {
        Nuki::CmdResult cmd_result = (Nuki::CmdResult)-1;
        bool is_advanced = false;

        if (strcmp(config, "single_button_press_action") == 0) {
            NukiLock::ButtonPressAction action = button_press_action_to_enum(value);
            cmd_result = nuki_lock_.setSingleButtonPressAction(action);
            is_advanced = true;
        } else if (strcmp(config, "double_button_press_action") == 0) {
            NukiLock::ButtonPressAction action = button_press_action_to_enum(value);
            cmd_result = nuki_lock_.setDoubleButtonPressAction(action);
            is_advanced = true;
        } else if (strcmp(config, "fob_action_1") == 0) {
            const uint8_t action = fob_action_to_int(value);
            if (action != 99) cmd_result = nuki_lock_.setFobAction(1, action);
        } else if (strcmp(config, "fob_action_2") == 0) {
            const uint8_t action = fob_action_to_int(value);
            if (action != 99) cmd_result = nuki_lock_.setFobAction(2, action);
        } else if (strcmp(config, "fob_action_3") == 0) {
            const uint8_t action = fob_action_to_int(value);
            if (action != 99) cmd_result = nuki_lock_.setFobAction(3, action);
        } else if (strcmp(config, "timezone") == 0) {
            Nuki::TimeZoneId tzid = timezone_to_enum(value);
            cmd_result = nuki_lock_.setTimeZoneId(tzid);
        } else if (strcmp(config, "advertising_mode") == 0) {
            Nuki::AdvertisingMode mode = advertising_mode_to_enum(value);
            cmd_result = nuki_lock_.setAdvertisingMode(mode);
        } else if (!nuki_lock_.isLockUltra() && strcmp(config, "battery_type") == 0) {
            Nuki::BatteryType type = battery_type_to_enum(value);
            cmd_result = nuki_lock_.setBatteryType(type);
            is_advanced = true;
        } else if (nuki_lock_.isLockUltra() && strcmp(config, "motor_speed") == 0) {
            NukiLock::MotorSpeed speed = motor_speed_to_enum(value);
            cmd_result = nuki_lock_.setMotorSpeed(speed);
            is_advanced = true;
        }

        esp_task_wdt_reset();

        if (cmd_result == Nuki::CmdResult::Success) {
            if (is_advanced) advanced_config_update_.store(true);
            else config_update_.store(true);
        } else {
            ESP_LOGE(TAG, "Saving setting %s failed (result %d)", config, cmd_result);
        }
        return cmd_result;
    };

    // ── Helper: execute a config-number command on nuki_lock_ ──────────

    auto exec_config_number = [this](const char* config, float value) -> Nuki::CmdResult {
        Nuki::CmdResult cmd_result = (Nuki::CmdResult)-1;
        bool is_advanced = false;

        if (strcmp(config, "led_brightness") == 0) {
            cmd_result = nuki_lock_.setLedBrightness(value);
        } else if (strcmp(config, "timezone_offset") == 0) {
            if (value >= -60 && value <= 60) {
                cmd_result = nuki_lock_.setTimeZoneOffset(value);
            }
        } else if (strcmp(config, "lock_n_go_timeout") == 0) {
            if (value >= 5 && value <= 60) {
                cmd_result = nuki_lock_.setLockNgoTimeout(value);
                is_advanced = true;
            }
        } else if (strcmp(config, "auto_lock_timeout") == 0) {
            if (value >= 30 && value <= 1800) {
                cmd_result = nuki_lock_.setAutoLockTimeOut(value);
                is_advanced = true;
            }
        } else if (strcmp(config, "unlatch_duration") == 0) {
            if (value >= 1 && value <= 30) {
                cmd_result = nuki_lock_.setUnlatchDuration(value);
                is_advanced = true;
            }
        } else if (strcmp(config, "unlocked_position_offset") == 0) {
            if (value >= -90 && value <= 180) {
                cmd_result = nuki_lock_.setUnlockedPositionOffsetDegrees(value);
                is_advanced = true;
            }
        } else if (strcmp(config, "locked_position_offset") == 0) {
            if (value >= -180 && value <= 90) {
                cmd_result = nuki_lock_.setLockedPositionOffsetDegrees(value);
                is_advanced = true;
            }
        } else if (strcmp(config, "single_locked_position_offset") == 0) {
            if (value >= -180 && value <= 180) {
                cmd_result = nuki_lock_.setSingleLockedPositionOffsetDegrees(value);
                is_advanced = true;
            }
        } else if (strcmp(config, "unlocked_to_locked_transition_offset") == 0) {
            if (value >= -180 && value <= 180) {
                cmd_result = nuki_lock_.setUnlockedToLockedTransitionOffsetDegrees(value);
                is_advanced = true;
            }
        }

        esp_task_wdt_reset();

        if (cmd_result == Nuki::CmdResult::Success) {
            if (is_advanced) advanced_config_update_.store(true);
            else config_update_.store(true);
        } else {
            ESP_LOGE(TAG, "Saving setting %s failed (result %d)", config, cmd_result);
        }
        return cmd_result;
    };

    // ── Main BLE task loop ─────────────────────────────────────────────

    uint32_t last_cmd_time = 0;
    uint32_t cooldown = 0;
    uint32_t last_keepalive = 0;

    while (true) {
        scanner_.update();
        nuki_lock_.updateConnectionState();

        uint32_t now = millis();

        // ── Process one command from the queue (non-blocking) ──────────

        BleCmdMsg cmd;
        if (xQueueReceive(cmd_queue_, &cmd, 0) == pdTRUE) {
            switch (cmd.cmd) {

                case BleCmd::LockAction: {
                    Nuki::CmdResult result = nuki_lock_.lockAction(cmd.lock_action);
                    esp_task_wdt_reset();

                    char la_str[30] = {0};
                    NukiLock::lockactionToString(cmd.lock_action, la_str);
                    char res_str[30] = {0};
                    NukiLock::cmdResultToString(result, res_str);

                    if (result == Nuki::CmdResult::Success) {
                        ESP_LOGI(TAG, "lockAction %s (%d) -> %s", la_str, cmd.lock_action, res_str);
                    } else {
                        ESP_LOGE(TAG, "lockAction %s (%d) -> %s", la_str, cmd.lock_action, res_str);
                    }

                    BleResultMsg rmsg{};
                    rmsg.type = BleResultType::ActionResult;
                    rmsg.success = (result == Nuki::CmdResult::Success);
                    xQueueSend(result_queue_, &rmsg, portMAX_DELAY);

                    status_update_.store(true);
                    cooldown = rmsg.success ? COOLDOWN_COMMANDS_EXTENDED_MILLIS : COOLDOWN_COMMANDS_MILLIS;
                    break;
                }

                case BleCmd::RequestStatus: {
                    BleResultMsg rmsg = do_status_request();
                    xQueueSend(result_queue_, &rmsg, portMAX_DELAY);
                    cooldown = COOLDOWN_COMMANDS_MILLIS;
                    break;
                }

                case BleCmd::RequestConfig: {
                    BleResultMsg rmsg = do_config_request();
                    xQueueSend(result_queue_, &rmsg, portMAX_DELAY);
                    cooldown = COOLDOWN_COMMANDS_MILLIS;
                    break;
                }

                case BleCmd::RequestAdvancedConfig: {
                    BleResultMsg rmsg = do_advanced_config_request();
                    xQueueSend(result_queue_, &rmsg, portMAX_DELAY);
                    cooldown = COOLDOWN_COMMANDS_MILLIS;
                    break;
                }

                case BleCmd::RequestAuthData: {
                    do_auth_data_request();
                    cooldown = COOLDOWN_COMMANDS_MILLIS;
                    break;
                }

                case BleCmd::RequestEventLogs: {
                    do_event_log_request();
                    cooldown = COOLDOWN_COMMANDS_MILLIS;
                    break;
                }

                case BleCmd::Pair: {
                    Nuki::AuthorizationIdType type = cmd.pair.as_app ?
                        Nuki::AuthorizationIdType::App : Nuki::AuthorizationIdType::Bridge;

                    esp_task_wdt_reset();
                    bool paired = nuki_lock_.pairNuki(type) == Nuki::PairingResult::Success;
                    esp_task_wdt_reset();

                    if (paired) {
                        paired_.store(true);

                        BleResultMsg srmsg = do_status_request();
                        xQueueSend(result_queue_, &srmsg, portMAX_DELAY);
                    }

                    BleResultMsg rmsg{};
                    rmsg.type = BleResultType::PairResult;
                    rmsg.success = paired;
                    if (paired) {
                        rmsg.cfg.is_ultra = nuki_lock_.isLockUltra();
                    }
                    xQueueSend(result_queue_, &rmsg, portMAX_DELAY);

                    cooldown = COOLDOWN_COMMANDS_EXTENDED_MILLIS;
                    break;
                }

                case BleCmd::Unpair: {
                    nuki_lock_.unPairNuki();
                    paired_.store(false);

                    BleResultMsg rmsg{};
                    rmsg.type = BleResultType::UnpairDone;
                    rmsg.success = true;
                    xQueueSend(result_queue_, &rmsg, portMAX_DELAY);
                    break;
                }

                case BleCmd::SetSecurityPin:
                case BleCmd::SavePin: {
                    uint32_t pin = cmd.security_pin.pin;
                    bool save_result = nuki_lock_.isLockUltra() ?
                        nuki_lock_.saveUltraPincode(pin) :
                        nuki_lock_.saveSecurityPincode(static_cast<uint16_t>(pin));
                    esp_task_wdt_reset();

                    BleResultMsg rmsg{};
                    rmsg.type = BleResultType::PinSaveResult;
                    rmsg.success = save_result;
                    xQueueSend(result_queue_, &rmsg, portMAX_DELAY);
                    break;
                }

                case BleCmd::VerifyPin: {
                    Nuki::CmdResult result = Nuki::CmdResult::Failed;
                    for (int i = 0; i < 4; i++) {
                        result = nuki_lock_.verifySecurityPin();
                        esp_task_wdt_reset();
                        if (result == Nuki::CmdResult::Success) break;
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }

                    BleResultMsg rmsg{};
                    rmsg.type = BleResultType::PinVerifyResult;
                    rmsg.success = (result == Nuki::CmdResult::Success);
                    xQueueSend(result_queue_, &rmsg, portMAX_DELAY);
                    break;
                }

                case BleCmd::SetConfigSwitch: {
                    Nuki::CmdResult cr = exec_config_switch(cmd.config_switch.key, cmd.config_switch.value);

                    BleResultMsg rmsg{};
                    rmsg.type = BleResultType::CommandResult;
                    rmsg.success = (cr == Nuki::CmdResult::Success);
                    xQueueSend(result_queue_, &rmsg, portMAX_DELAY);
                    cooldown = COOLDOWN_COMMANDS_MILLIS;
                    break;
                }

                case BleCmd::SetConfigSelect: {
                    Nuki::CmdResult cr = exec_config_select(cmd.config_select.key, cmd.config_select.value);

                    BleResultMsg rmsg{};
                    rmsg.type = BleResultType::CommandResult;
                    rmsg.success = (cr == Nuki::CmdResult::Success);
                    xQueueSend(result_queue_, &rmsg, portMAX_DELAY);
                    cooldown = COOLDOWN_COMMANDS_MILLIS;
                    break;
                }

                case BleCmd::SetConfigNumber: {
                    Nuki::CmdResult cr = exec_config_number(cmd.config_number.key, cmd.config_number.value);

                    BleResultMsg rmsg{};
                    rmsg.type = BleResultType::CommandResult;
                    rmsg.success = (cr == Nuki::CmdResult::Success);
                    xQueueSend(result_queue_, &rmsg, portMAX_DELAY);
                    cooldown = COOLDOWN_COMMANDS_MILLIS;
                    break;
                }

                case BleCmd::RequestCalibration: {
                    Nuki::CmdResult result = nuki_lock_.requestCalibration();
                    esp_task_wdt_reset();

                    if (result == Nuki::CmdResult::Success) {
                        ESP_LOGI(TAG, "Calibration requested successfully");
                    } else {
                        ESP_LOGE(TAG, "Failed to request calibration (result %d)", result);
                    }

                    BleResultMsg rmsg{};
                    rmsg.type = BleResultType::CommandResult;
                    rmsg.success = (result == Nuki::CmdResult::Success);
                    xQueueSend(result_queue_, &rmsg, portMAX_DELAY);
                    break;
                }

                case BleCmd::PrintKeypadEntries: {
                    Nuki::CmdResult result = nuki_lock_.retrieveKeypadEntries(0, 0xffff);
                    esp_task_wdt_reset();

                    if (result == Nuki::CmdResult::Success) {
                        ESP_LOGI(TAG, "retrieveKeypadEntries success");
                        std::list<NukiLock::KeypadEntry> entries;
                        nuki_lock_.getKeypadEntries(&entries);

                        entries.sort([](const NukiLock::KeypadEntry& a, const NukiLock::KeypadEntry& b) {
                            return a.codeId < b.codeId;
                        });

                        keypad_code_ids_.clear();
                        keypad_code_ids_.reserve(entries.size());
                        for (const auto& entry : entries) {
                            keypad_code_ids_.push_back(entry.codeId);
                            ESP_LOGI(TAG, "keypad #%d %s is %s", entry.codeId, entry.name, entry.enabled ? "enabled" : "disabled");
                        }
                    } else {
                        ESP_LOGE(TAG, "retrieveKeypadEntries failed (result %d)", result);
                    }

                    BleResultMsg rmsg{};
                    rmsg.type = BleResultType::CommandResult;
                    rmsg.success = (result == Nuki::CmdResult::Success);
                    xQueueSend(result_queue_, &rmsg, portMAX_DELAY);
                    break;
                }

                case BleCmd::AddKeypadEntry: {
                    NukiLock::NewKeypadEntry entry;
                    memset(&entry, 0, sizeof(entry));
                    memcpy(entry.name, cmd.add_keypad.name, sizeof(entry.name));
                    entry.code = cmd.add_keypad.code;

                    Nuki::CmdResult result = nuki_lock_.addKeypadEntry(entry);
                    esp_task_wdt_reset();

                    if (result == Nuki::CmdResult::Success) {
                        ESP_LOGI(TAG, "add_keypad_entry is successful");
                    } else {
                        ESP_LOGE(TAG, "add_keypad_entry: addKeypadEntry failed (result %d)", result);
                    }

                    BleResultMsg rmsg{};
                    rmsg.type = BleResultType::CommandResult;
                    rmsg.success = (result == Nuki::CmdResult::Success);
                    xQueueSend(result_queue_, &rmsg, portMAX_DELAY);
                    break;
                }

                case BleCmd::UpdateKeypadEntry: {
                    NukiLock::UpdatedKeypadEntry entry;
                    memset(&entry, 0, sizeof(entry));
                    entry.codeId = cmd.update_keypad.id;
                    size_t name_len = strlen(cmd.update_keypad.name);
                    memcpy(entry.name, cmd.update_keypad.name, name_len > 20 ? 20 : name_len);
                    entry.code = cmd.update_keypad.code;
                    entry.enabled = cmd.update_keypad.enabled ? 1 : 0;

                    Nuki::CmdResult result = nuki_lock_.updateKeypadEntry(entry);
                    esp_task_wdt_reset();

                    if (result == Nuki::CmdResult::Success) {
                        ESP_LOGI(TAG, "update_keypad_entry is successful");
                    } else {
                        ESP_LOGE(TAG, "update_keypad_entry: updateKeypadEntry failed (result %d)", result);
                    }

                    BleResultMsg rmsg{};
                    rmsg.type = BleResultType::CommandResult;
                    rmsg.success = (result == Nuki::CmdResult::Success);
                    xQueueSend(result_queue_, &rmsg, portMAX_DELAY);
                    break;
                }

                case BleCmd::DeleteKeypadEntry: {
                    Nuki::CmdResult result = nuki_lock_.deleteKeypadEntry(cmd.delete_keypad.id);
                    esp_task_wdt_reset();

                    if (result == Nuki::CmdResult::Success) {
                        ESP_LOGI(TAG, "delete_keypad_entry is successful");
                    } else {
                        ESP_LOGE(TAG, "delete_keypad_entry: deleteKeypadEntry failed (result %d)", result);
                    }

                    BleResultMsg rmsg{};
                    rmsg.type = BleResultType::CommandResult;
                    rmsg.success = (result == Nuki::CmdResult::Success);
                    xQueueSend(result_queue_, &rmsg, portMAX_DELAY);
                    break;
                }
            } // switch

            last_cmd_time = millis();
        } // xQueueReceive

        // ── Auto-poll: service atomic flags set by main-loop intervals ─

        if (now - last_cmd_time >= cooldown) {
            if (status_update_.load()) {
                status_update_.store(false);
                ESP_LOGD(TAG, "Auto-poll: requesting status...");

                BleResultMsg rmsg = do_status_request();
                xQueueSend(result_queue_, &rmsg, portMAX_DELAY);

                last_cmd_time = millis();
                cooldown = COOLDOWN_COMMANDS_MILLIS;
            }
            else if (config_update_.load()) {
                config_update_.store(false);
                ESP_LOGD(TAG, "Auto-poll: requesting config...");

                BleResultMsg rmsg = do_config_request();
                xQueueSend(result_queue_, &rmsg, portMAX_DELAY);

                last_cmd_time = millis();
                cooldown = COOLDOWN_COMMANDS_MILLIS;
            }
            else if (advanced_config_update_.load()) {
                advanced_config_update_.store(false);
                ESP_LOGD(TAG, "Auto-poll: requesting advanced config...");

                BleResultMsg rmsg = do_advanced_config_request();
                xQueueSend(result_queue_, &rmsg, portMAX_DELAY);

                last_cmd_time = millis();
                cooldown = COOLDOWN_COMMANDS_MILLIS;
            }
            else if (auth_data_update_.load()) {
                auth_data_update_.store(false);
                ESP_LOGD(TAG, "Auto-poll: requesting auth data...");

                do_auth_data_request();

                last_cmd_time = millis();
                cooldown = COOLDOWN_COMMANDS_MILLIS;
            }
            else if (event_log_update_.load()) {
                event_log_update_.store(false);
                ESP_LOGD(TAG, "Auto-poll: requesting event logs...");

                do_event_log_request();

                last_cmd_time = millis();
                cooldown = COOLDOWN_COMMANDS_MILLIS;
            }
            // ── Pairing mode ───────────────────────────────────────────
            else if (!paired_.load() && pairing_mode_.load()) {
                Nuki::AuthorizationIdType type = pairing_as_app_.value_or(false) ?
                    Nuki::AuthorizationIdType::App : Nuki::AuthorizationIdType::Bridge;

                ESP_LOGI(TAG, "Attempting to pair...");
                esp_task_wdt_reset();
                bool paired = nuki_lock_.pairNuki(type) == Nuki::PairingResult::Success;
                esp_task_wdt_reset();

                if (paired) {
                    paired_.store(true);

                    BleResultMsg srmsg = do_status_request();
                    xQueueSend(result_queue_, &srmsg, portMAX_DELAY);
                }

                BleResultMsg rmsg{};
                rmsg.type = BleResultType::PairResult;
                rmsg.success = paired;
                if (paired) {
                    rmsg.cfg.is_ultra = nuki_lock_.isLockUltra();
                }
                xQueueSend(result_queue_, &rmsg, portMAX_DELAY);

                last_cmd_time = millis();
                cooldown = COOLDOWN_COMMANDS_EXTENDED_MILLIS;
            }
            // ── Keep-alive: periodic status poll ───────────────────────
            else if (paired_.load() && (now - last_keepalive >= KEEP_ALIVE_INTERVAL_MILLIS)) {
                status_update_.store(true);
                last_keepalive = now;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  notify() — called from BLE context (Core 0)
// ═══════════════════════════════════════════════════════════════════════════

void NukiLockComponent::notify(Nuki::EventType event_type) {
    if (!result_queue_) return;

    BleResultMsg msg{};
    msg.type = BleResultType::NotifyEvent;
    msg.event_type = event_type;
    xQueueSend(result_queue_, &msg, pdMS_TO_TICKS(100));
}

// ═══════════════════════════════════════════════════════════════════════════
//  control() — runs on Core 1
// ═══════════════════════════════════════════════════════════════════════════

void NukiLockComponent::control(const lock::LockCall &call) {
    if (!paired_.load()) {
        ESP_LOGE(TAG, "Lock is not paired, cannot execute lock action");
        return;
    }

    lock::LockState state = *call.get_state();
    NukiLock::LockAction action;

    switch(state) {
        case lock::LOCK_STATE_LOCKED:
            action = NukiLock::LockAction::Lock;
            break;

        case lock::LOCK_STATE_UNLOCKED: {
            action = NukiLock::LockAction::Unlock;

            if (this->open_latch_) {
                action = NukiLock::LockAction::Unlatch;
            }

            if (this->lock_n_go_) {
                action = NukiLock::LockAction::LockNgo;
            }

            this->open_latch_ = false;
            this->lock_n_go_ = false;
            break;
        }

        default:
            ESP_LOGE(TAG, "lockAction unsupported state");
            return;
    }

    switch (action) {
        case NukiLock::LockAction::Unlatch:
        case NukiLock::LockAction::Unlock:
            this->publish_state(lock::LOCK_STATE_UNLOCKING);
            break;
        case NukiLock::LockAction::FullLock:
        case NukiLock::LockAction::Lock:
        case NukiLock::LockAction::LockNgo:
            this->publish_state(lock::LOCK_STATE_LOCKING);
            break;
        default:
            break;
    }

    this->lock_action_ = action;
    this->action_attempts_ = MAX_ACTION_ATTEMPTS;

    BleCmdMsg msg{};
    msg.cmd = BleCmd::LockAction;
    msg.lock_action = action;
    send_cmd(msg);

    char lock_action_as_string[30] = {0};
    NukiLock::lockactionToString(action, lock_action_as_string);
    ESP_LOGI(TAG, "Lock action enqueued: %s (%d)", lock_action_as_string, action);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Public API methods
// ═══════════════════════════════════════════════════════════════════════════

void NukiLockComponent::set_security_pin(uint32_t new_pin) {
    ESP_LOGI(TAG, "Setting security pin: %u", new_pin);

    if (new_pin > 999999) {
        ESP_LOGE(TAG, "Invalid pin: %u (max. 6 digits/999999) - abort.", new_pin);
        return;
    }

    if (new_pin == 0 && this->security_pin_ != 0) {
        ESP_LOGI(TAG, "Clearing security pin override");
    }
    this->security_pin_ = new_pin;

    const uint32_t pin_to_use = this->security_pin_ != 0 ? this->security_pin_ : this->security_pin_config_.value_or(0);

    if (pin_to_use == 0) {
        this->pin_state_ = PinState::NotSet;
        ESP_LOGW(TAG, "Security pin is not set! Some functions may be unavailable.");
        this->save_settings();
        this->publish_pin_state();
        return;
    }

    this->pin_state_ = PinState::Set;
    this->save_settings();
    this->publish_pin_state();

    BleCmdMsg msg{};
    msg.cmd = BleCmd::SetSecurityPin;
    msg.security_pin.pin = pin_to_use;
    send_cmd(msg);
}

void NukiLockComponent::unpair() {
    if (!paired_.load()) {
        ESP_LOGE(TAG, "Lock is not paired, cannot unpair");
        return;
    }

    BleCmdMsg msg{};
    msg.cmd = BleCmd::Unpair;
    send_cmd(msg);
}

void NukiLockComponent::set_pairing_mode(bool enabled) {
    this->pairing_mode_.store(enabled);

    #ifdef USE_SWITCH
    if (this->pairing_mode_switch_ != nullptr) {
        this->pairing_mode_switch_->publish_state(enabled);
    }
    #endif

    cancel_timeout("pairing_mode_timeout");

    if (enabled) {
        ESP_LOGI(TAG, "Pairing Mode turned on for %d seconds", this->pairing_mode_timeout_);
        this->pairing_mode_on_callback_.call();

        if (this->security_pin_ != 0) {
            ESP_LOGW(TAG, "Note: Using security pin override to pair, not yaml config pin!");
        } else if(this->security_pin_config_.value_or(0) != 0) {
            ESP_LOGD(TAG, "Using security pin from yaml config to pair.");
        } else {
            ESP_LOGW(TAG, "Note: The security pin is crucial to pair a Smart Lock Ultra but is currently not set.");
        }

        ESP_LOGD(TAG, "ESPHome PIN (override): %d", this->security_pin_);
        ESP_LOGD(TAG, "ESPHome PIN (YAML): %d", this->security_pin_config_.value_or(0));

        ESP_LOGI(TAG, "Waiting for Nuki to enter pairing mode...");

        this->set_timeout("pairing_mode_timeout", this->pairing_mode_timeout_ * 1000, [this]()
        {
            ESP_LOGV(TAG, "Pairing timed out, turning off pairing mode");
            this->set_pairing_mode(false);
        });
    } else {
        ESP_LOGI(TAG, "Pairing Mode turned off");
        this->pairing_mode_off_callback_.call();
    }
}

void NukiLockComponent::request_calibration() {
    if (!paired_.load()) {
        ESP_LOGE(TAG, "Lock is not paired, cannot request calibration");
        return;
    }

    BleCmdMsg msg{};
    msg.cmd = BleCmd::RequestCalibration;
    send_cmd(msg);
}

void NukiLockComponent::lock_n_go() {
    this->lock_n_go_ = true;
    this->unlock();
}

void NukiLockComponent::print_keypad_entries() {
    if (!paired_.load()) {
        ESP_LOGE(TAG, "Lock is not paired, cannot retrieve keypad entries");
        return;
    }

    if (!keypad_paired_) {
        ESP_LOGE(TAG, "Keypad is not paired to Nuki");
        return;
    }

    if(this->pin_state_ != PinState::Valid) {
        ESP_LOGW(TAG, "It seems like you did not set a valid pin!");
        return;
    }

    BleCmdMsg msg{};
    msg.cmd = BleCmd::PrintKeypadEntries;
    send_cmd(msg);
}

void NukiLockComponent::add_keypad_entry(std::string name, int32_t code) {
    if (!paired_.load()) {
        ESP_LOGE(TAG, "Lock is not paired, cannot add keypad entry");
        return;
    }

    if (!keypad_paired_) {
        ESP_LOGE(TAG, "Keypad is not paired to Nuki");
        return;
    }

    if(this->pin_state_ != PinState::Valid) {
        ESP_LOGW(TAG, "It seems like you did not set a valid pin!");
        return;
    }

    if (!(valid_keypad_name(name) && valid_keypad_code(code))) {
        ESP_LOGE(TAG, "add_keypad_entry invalid parameters");
        return;
    }

    BleCmdMsg msg{};
    msg.cmd = BleCmd::AddKeypadEntry;
    memset(msg.add_keypad.name, 0, sizeof(msg.add_keypad.name));
    size_t len = name.length();
    memcpy(msg.add_keypad.name, name.c_str(), len > 20 ? 20 : len);
    msg.add_keypad.code = code;
    send_cmd(msg);
}

void NukiLockComponent::update_keypad_entry(int32_t id, std::string name, int32_t code, bool enabled) {
    if (!paired_.load()) {
        ESP_LOGE(TAG, "Lock is not paired, cannot update keypad entry");
        return;
    }

    if (!keypad_paired_) {
        ESP_LOGE(TAG, "keypad is not paired to Nuki");
        return;
    }

    if(this->pin_state_ != PinState::Valid) {
        ESP_LOGW(TAG, "It seems like you did not set a valid pin!");
        return;
    }

    if (!(valid_keypad_id(id) && valid_keypad_name(name) && valid_keypad_code(code))) {
        ESP_LOGE(TAG, "update_keypad_entry invalid parameters");
        return;
    }

    BleCmdMsg msg{};
    msg.cmd = BleCmd::UpdateKeypadEntry;
    msg.update_keypad.id = id;
    memset(msg.update_keypad.name, 0, sizeof(msg.update_keypad.name));
    size_t len = name.length();
    memcpy(msg.update_keypad.name, name.c_str(), len > 20 ? 20 : len);
    msg.update_keypad.code = code;
    msg.update_keypad.enabled = enabled;
    send_cmd(msg);
}

void NukiLockComponent::delete_keypad_entry(int32_t id) {
    if (!paired_.load()) {
        ESP_LOGE(TAG, "Lock is not paired, cannot delete keypad entry");
        return;
    }

    if (!keypad_paired_) {
        ESP_LOGE(TAG, "keypad is not paired to Nuki");
        return;
    }

    if(this->pin_state_ != PinState::Valid) {
        ESP_LOGW(TAG, "It seems like you did not set a valid pin!");
        return;
    }

    if (!valid_keypad_id(id)) {
        ESP_LOGE(TAG, "delete_keypad_entry invalid parameters");
        return;
    }

    BleCmdMsg msg{};
    msg.cmd = BleCmd::DeleteKeypadEntry;
    msg.delete_keypad.id = id;
    send_cmd(msg);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Config setters — enqueue commands to BLE task
// ═══════════════════════════════════════════════════════════════════════════

#ifdef USE_SWITCH
void NukiLockComponent::set_config_switch(const char* config, bool value) {
    if (!paired_.load()) {
        ESP_LOGE(TAG, "Lock is not paired, cannot change setting %s", config);
        return;
    }

    BleCmdMsg msg{};
    msg.cmd = BleCmd::SetConfigSwitch;
    strncpy(msg.config_switch.key, config, sizeof(msg.config_switch.key) - 1);
    msg.config_switch.key[sizeof(msg.config_switch.key) - 1] = '\0';
    msg.config_switch.value = value;
    send_cmd(msg);
}
#endif

#ifdef USE_SELECT
void NukiLockComponent::set_config_select(const char* config, const char* value) {
    if (!paired_.load()) {
        ESP_LOGE(TAG, "Lock is not paired, cannot change setting %s", config);
        return;
    }

    BleCmdMsg msg{};
    msg.cmd = BleCmd::SetConfigSelect;
    strncpy(msg.config_select.key, config, sizeof(msg.config_select.key) - 1);
    msg.config_select.key[sizeof(msg.config_select.key) - 1] = '\0';
    strncpy(msg.config_select.value, value, sizeof(msg.config_select.value) - 1);
    msg.config_select.value[sizeof(msg.config_select.value) - 1] = '\0';
    send_cmd(msg);
}
#endif

#ifdef USE_NUMBER
void NukiLockComponent::set_config_number(const char* config, float value) {
    if (!paired_.load()) {
        ESP_LOGE(TAG, "Lock is not paired, cannot change setting %s", config);
        return;
    }

    BleCmdMsg msg{};
    msg.cmd = BleCmd::SetConfigNumber;
    strncpy(msg.config_number.key, config, sizeof(msg.config_number.key) - 1);
    msg.config_number.key[sizeof(msg.config_number.key) - 1] = '\0';
    msg.config_number.value = value;
    send_cmd(msg);
}
#endif

// ═══════════════════════════════════════════════════════════════════════════
//  dump_config()
// ═══════════════════════════════════════════════════════════════════════════

void NukiLockComponent::dump_config() {
    ESP_LOGCONFIG(TAG, "nuki_lock:");

    #ifdef USE_API_HOMEASSISTANT_SERVICES
    if (strcmp(this->event_, "esphome.none") != 0) {
        ESP_LOGCONFIG(TAG, "  Event: %s", this->event_);
    } else {
        ESP_LOGCONFIG(TAG, "  Event: Disabled (event name set to none)");
    }
    #else
    ESP_LOGCONFIG(TAG, "  Event: Disabled (Home Assistant services not enabled)");
    #endif

    ESP_LOGCONFIG(TAG, "  Pairing Identity: %s", this->pairing_as_app_.value_or(false) ? "App" : "Bridge");
    ESP_LOGCONFIG(TAG, "  Is Paired: %s", YESNO(this->is_paired()));

    ESP_LOGCONFIG(TAG, "  Pairing mode timeout: %us", this->pairing_mode_timeout_);
    ESP_LOGCONFIG(TAG, "  Configuration query interval: %us", this->query_interval_config_);
    ESP_LOGCONFIG(TAG, "  Auth Data query interval: %us", this->query_interval_auth_data_);
    ESP_LOGCONFIG(TAG, "  BLE general timeout: %us", this->ble_general_timeout_);
    ESP_LOGCONFIG(TAG, "  BLE command timeout: %us", this->ble_command_timeout_);

    char pin_state_as_string[30] = {0};
    this->pin_state_to_string(this->pin_state_, pin_state_as_string);
    ESP_LOGCONFIG(TAG, "  Last known security pin state: %s", pin_state_as_string);

    LOG_LOCK(TAG, "Nuki Lock", this);
    #ifdef USE_BINARY_SENSOR
    LOG_BINARY_SENSOR(TAG, "Connected", this->connected_binary_sensor_);
    LOG_BINARY_SENSOR(TAG, "Paired", this->paired_binary_sensor_);
    LOG_BINARY_SENSOR(TAG, "Battery Critical", this->battery_critical_binary_sensor_);
    LOG_BINARY_SENSOR(TAG, "Door Sensor", this->door_sensor_binary_sensor_);
    #endif
    #ifdef USE_TEXT_SENSOR
    LOG_TEXT_SENSOR(TAG, "Door Sensor State", this->door_sensor_state_text_sensor_);
    LOG_TEXT_SENSOR(TAG, "Last Unlock User", this->last_unlock_user_text_sensor_);
    LOG_TEXT_SENSOR(TAG, "Last Lock Action", this->last_lock_action_text_sensor_);
    LOG_TEXT_SENSOR(TAG, "Last Lock Action Trigger", this->last_lock_action_trigger_text_sensor_);
    LOG_TEXT_SENSOR(TAG, "Pin Status", this->pin_state_text_sensor_);
    #endif
    #ifdef USE_SENSOR
    LOG_SENSOR(TAG, "Battery Level", this->battery_level_sensor_);
    LOG_SENSOR(TAG, "Bluetooth Signal", this->bt_signal_sensor_);
    #endif
    #ifdef USE_BUTTON
    LOG_BUTTON(TAG, "Unpair", this->unpair_button_);
    LOG_BUTTON(TAG, "Request Calibration", this->request_calibration_button_);
    #endif
    #ifdef USE_SWITCH
    LOG_SWITCH(TAG, "Pairing Mode", this->pairing_mode_switch_);
    LOG_SWITCH(TAG, "Pairing Enabled", this->pairing_enabled_switch_);
    LOG_SWITCH(TAG, "Auto Unlatch Enabled", this->auto_unlatch_enabled_switch_);
    LOG_SWITCH(TAG, "Button Enabled", this->button_enabled_switch_);
    LOG_SWITCH(TAG, "LED Enabled", this->led_enabled_switch_);
    LOG_SWITCH(TAG, "Night Mode Enabled", this->nightmode_enabled_switch_);
    LOG_SWITCH(TAG, "Night Mode Auto Lock", this->night_mode_auto_lock_enabled_switch_);
    LOG_SWITCH(TAG, "Night Mode Auto Unlock Disabled", this->night_mode_auto_unlock_disabled_switch_);
    LOG_SWITCH(TAG, "Night Mode Immediate Lock On Start", this->night_mode_immediate_lock_on_start_switch_);
    LOG_SWITCH(TAG, "Auto Lock", this->auto_lock_enabled_switch_);
    LOG_SWITCH(TAG, "Auto Unlock Disabled", this->auto_unlock_disabled_switch_);
    LOG_SWITCH(TAG, "Immediate Auto Lock", this->immediate_auto_lock_enabled_switch_);
    LOG_SWITCH(TAG, "Automatic Updates", this->auto_update_enabled_switch_);
    LOG_SWITCH(TAG, "Single Lock Enabled", this->single_lock_enabled_switch_);
    LOG_SWITCH(TAG, "DST Mode Enabled", this->dst_mode_enabled_switch_);
    LOG_SWITCH(TAG, "Slow Speed During Night Mode Enabled", this->slow_speed_during_night_mode_enabled_switch_);
    LOG_SWITCH(TAG, "Detached Cylinder Enabled", this->detached_cylinder_enabled_switch_);
    #endif
    #ifdef USE_NUMBER
    LOG_NUMBER(TAG, "LED Brightness", this->led_brightness_number_);
    LOG_NUMBER(TAG, "Timezone Offset", this->timezone_offset_number_);
    LOG_NUMBER(TAG, "LockNGo Timeout", this->lock_n_go_timeout_number_);
    LOG_NUMBER(TAG, "Auto Lock Timeout", this->auto_lock_timeout_number_);
    LOG_NUMBER(TAG, "Unlatch Duration", this->unlatch_duration_number_);
    LOG_NUMBER(TAG, "Unlocked Position Offset Degrees", this->unlocked_position_offset_number_);
    LOG_NUMBER(TAG, "Locked Position Offset Degrees", this->locked_position_offset_number_);
    LOG_NUMBER(TAG, "Single Locked Position Offset Degrees", this->single_locked_position_offset_number_);
    LOG_NUMBER(TAG, "Unlocked To Locked Transition Offset Degrees", this->unlocked_to_locked_transition_offset_number_);
    #endif
    #ifdef USE_SELECT
    LOG_SELECT(TAG, "Single Button Press Action", this->single_button_press_action_select_);
    LOG_SELECT(TAG, "Double Button Press Action", this->double_button_press_action_select_);
    LOG_SELECT(TAG, "Fob Action 1", this->fob_action_1_select_);
    LOG_SELECT(TAG, "Fob Action 2", this->fob_action_2_select_);
    LOG_SELECT(TAG, "Fob Action 3", this->fob_action_3_select_);
    LOG_SELECT(TAG, "Timezone", this->timezone_select_);
    LOG_SELECT(TAG, "Advertising Mode", this->advertising_mode_select_);
    LOG_SELECT(TAG, "Battery Type", this->battery_type_select_);
    LOG_SELECT(TAG, "Motor Speed", this->motor_speed_select_);
    #endif
}

// ═══════════════════════════════════════════════════════════════════════════
//  Entity delegates
// ═══════════════════════════════════════════════════════════════════════════

#ifdef USE_BUTTON
void NukiLockUnpairButton::press_action() {
    this->parent_->unpair();
}

void NukiLockRequestCalibrationButton::press_action() {
    this->parent_->request_calibration();
}
#endif

#ifdef USE_SELECT
void NukiLockSingleButtonPressActionSelect::control(const std::string &action) {
    this->parent_->set_config_select("single_button_press_action", action.c_str());
}

void NukiLockDoubleButtonPressActionSelect::control(const std::string &action) {
    this->parent_->set_config_select("double_button_press_action", action.c_str());
}

void NukiLockFobAction1Select::control(const std::string &action) {
    this->parent_->set_config_select("fob_action_1", action.c_str());
}

void NukiLockFobAction2Select::control(const std::string &action) {
    this->parent_->set_config_select("fob_action_2", action.c_str());
}

void NukiLockFobAction3Select::control(const std::string &action) {
    this->parent_->set_config_select("fob_action_3", action.c_str());
}

void NukiLockTimeZoneSelect::control(const std::string &zone) {
    this->parent_->set_config_select("timezone", zone.c_str());
}

void NukiLockAdvertisingModeSelect::control(const std::string &mode) {
    this->parent_->set_config_select("advertising_mode", mode.c_str());
}

void NukiLockBatteryTypeSelect::control(const std::string &mode) {
    this->parent_->set_config_select("battery_type", mode.c_str());
}

void NukiLockMotorSpeedSelect::control(const std::string &mode) {
    this->parent_->set_config_select("motor_speed", mode.c_str());
}
#endif

#ifdef USE_SWITCH
void NukiLockPairingModeSwitch::write_state(bool state) {
    this->parent_->set_pairing_mode(state);
}

void NukiLockPairingEnabledSwitch::write_state(bool state) {
    this->parent_->set_config_switch("pairing_enabled", state);
}

void NukiLockAutoUnlatchEnabledSwitch::write_state(bool state) {
    this->parent_->set_config_switch("auto_unlatch_enabled", state);
}

void NukiLockButtonEnabledSwitch::write_state(bool state) {
    this->parent_->set_config_switch("button_enabled", state);
}

void NukiLockLedEnabledSwitch::write_state(bool state) {
    this->parent_->set_config_switch("led_enabled", state);
}

void NukiLockNightModeEnabledSwitch::write_state(bool state) {
    this->parent_->set_config_switch("nightmode_enabled", state);
}

void NukiLockNightModeAutoLockEnabledSwitch::write_state(bool state) {
    this->parent_->set_config_switch("night_mode_auto_lock_enabled", state);
}

void NukiLockNightModeAutoUnlockDisabledSwitch::write_state(bool state) {
    this->parent_->set_config_switch("night_mode_auto_unlock_disabled", state);
}

void NukiLockNightModeImmediateLockOnStartEnabledSwitch::write_state(bool state) {
    this->parent_->set_config_switch("night_mode_immediate_lock_on_start", state);
}

void NukiLockAutoLockEnabledSwitch::write_state(bool state) {
    this->parent_->set_config_switch("auto_lock_enabled", state);
}

void NukiLockAutoUnlockDisabledSwitch::write_state(bool state) {
    this->parent_->set_config_switch("auto_unlock_disabled", state);
}

void NukiLockImmediateAutoLockEnabledSwitch::write_state(bool state) {
    this->parent_->set_config_switch("immediate_auto_lock_enabled", state);
}

void NukiLockAutoUpdateEnabledSwitch::write_state(bool state) {
    this->parent_->set_config_switch("auto_update_enabled", state);
}

void NukiLockSingleLockEnabledSwitch::write_state(bool state) {
    this->parent_->set_config_switch("single_lock_enabled", state);
}

void NukiLockDstModeEnabledSwitch::write_state(bool state) {
    this->parent_->set_config_switch("dst_mode_enabled", state);
}

void NukiLockAutoBatteryTypeDetectionEnabledSwitch::write_state(bool state) {
    this->parent_->set_config_switch("auto_battery_type_detection_enabled", state);
}

void NukiLockSlowSpeedDuringNightModeEnabledSwitch::write_state(bool state) {
    this->parent_->set_config_switch("slow_speed_during_night_mode_enabled", state);
}

void NukiLockDetachedCylinderEnabledSwitch::write_state(bool state) {
    this->parent_->set_config_switch("detached_cylinder_enabled", state);
}
#endif

#ifdef USE_NUMBER
void NukiLockLedBrightnessNumber::control(float value) {
    this->parent_->set_config_number("led_brightness", value);
}
void NukiLockTimeZoneOffsetNumber::control(float value) {
    this->parent_->set_config_number("timezone_offset", value);
}
void NukiLockLockNGoTimeoutNumber::control(float value) {
    this->parent_->set_config_number("lock_n_go_timeout", value);
}
void NukiLockAutoLockTimeoutNumber::control(float value) {
    this->parent_->set_config_number("auto_lock_timeout", value);
}
void NukiLockUnlatchDurationNumber::control(float value) {
    this->parent_->set_config_number("unlatch_duration", value);
}
void NukiLockUnlockedPositionOffsetDegreesNumber::control(float value) {
    this->parent_->set_config_number("unlocked_position_offset", value);
}
void NukiLockLockedPositionOffsetDegreesNumber::control(float value) {
    this->parent_->set_config_number("locked_position_offset", value);
}
void NukiLockSingleLockedPositionOffsetDegreesNumber::control(float value) {
    this->parent_->set_config_number("single_locked_position_offset", value);
}
void NukiLockUnlockedToLockedTransitionOffsetDegreesNumber::control(float value) {
    this->parent_->set_config_number("unlocked_to_locked_transition_offset", value);
}
#endif

// ═══════════════════════════════════════════════════════════════════════════
//  Callbacks
// ═══════════════════════════════════════════════════════════════════════════

void NukiLockComponent::add_pairing_mode_on_callback(std::function<void()> &&callback) {
    this->pairing_mode_on_callback_.add(std::move(callback));
}

void NukiLockComponent::add_pairing_mode_off_callback(std::function<void()> &&callback) {
    this->pairing_mode_off_callback_.add(std::move(callback));
}

void NukiLockComponent::add_paired_callback(std::function<void()> &&callback) {
    this->paired_callback_.add(std::move(callback));
}

void NukiLockComponent::add_event_log_received_callback(std::function<void(NukiLock::LogEntry)> &&callback) {
    this->event_log_received_callback_.add(std::move(callback));
}

} //namespace nuki_lock
} //namespace esphome
