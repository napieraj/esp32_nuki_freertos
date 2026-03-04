#include "nuki_pro.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include <esp_task_wdt.h>

namespace esphome {
namespace nuki_pro {

lock::LockState NukiProLock::nuki_to_esphome_state(NukiLock::LockState s) {
  switch (s) {
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

// ── Preference persistence (2026 API) ──────────────────────────────────

void NukiProLock::save_pairing_data() {
  bool paired = this->paired_.load();
  if (!this->paired_pref_.save(&paired)) {
    ESP_LOGW(TAG, "Failed to persist pairing data");
  }
}

bool NukiProLock::load_pairing_data(bool *paired) {
  if (paired == nullptr) {
    return false;
  }

  bool restored = false;
  if (this->paired_pref_.load(&restored)) {
    *paired = restored;
    ESP_LOGD(TAG, "Loaded pairing preference (paired=%s)", YESNO(restored));
    return true;
  }

  return false;
}

// ── Setup (Core 1) ────────────────────────────────────────────────────

void NukiProLock::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Nuki 5.0 Pro...");

  esp_task_wdt_config_t wdt = {.timeout_ms = 15000, .trigger_panic = false};
  esp_task_wdt_reconfigure(&wdt);

  // 2026 preference API: collision-free entity-scoped storage (version 1)
  this->paired_pref_ = this->make_entity_preference<bool>(1);
  bool stored_paired = false;
  bool has_stored_pairing = this->load_pairing_data(&stored_paired);

  // Construct NukiLock with configurable device ID (deferred from ctor
  // because NukiBle has no deviceId setter — must wait for set_device_id())
  this->nuki_lock_ =
      std::make_unique<NukiLock::NukiLock>("NukiESPHome", this->nuki_id_);
  this->nuki_lock_->setEventHandler(this);

  this->scanner_.initialize("ESPHomeNuki", true, 40, 40);
  this->scanner_.setScanDuration(0);

  this->nuki_lock_->saveUltraPincode(this->pin_, false);

  this->nuki_lock_->setDebugConnect(true);
  this->nuki_lock_->setDebugCommunication(false);
  this->nuki_lock_->setDebugReadableData(false);
  this->nuki_lock_->setDebugHexData(false);
  this->nuki_lock_->setDebugCommand(false);

  this->nuki_lock_->initialize();
  this->nuki_lock_->registerBleScanner(&this->scanner_);
  this->nuki_lock_->setConnectTimeout(BLE_CONNECT_TIMEOUT);
  this->nuki_lock_->setConnectRetries(BLE_CONNECT_RETRIES);
  this->nuki_lock_->setDisconnectTimeout(BLE_DISCONNECT_TIMEOUT);

  bool runtime_paired = this->nuki_lock_->isPairedWithLock();
  if (has_stored_pairing) {
    if (stored_paired != runtime_paired) {
      ESP_LOGW(
          TAG,
          "Pairing state mismatch (stored=%s runtime=%s); trusting runtime",
          YESNO(stored_paired), YESNO(runtime_paired));
    } else {
      ESP_LOGD(TAG, "Pairing state consistent (paired=%s)",
               YESNO(runtime_paired));
    }
  } else {
    ESP_LOGD(TAG, "No stored pairing preference; using runtime lock state");
  }

  this->paired_.store(runtime_paired);
  this->connected_.store(false);
  this->pairing_mode_.store(!runtime_paired);
  this->status_poll_requested_.store(runtime_paired);

  if (!has_stored_pairing || stored_paired != runtime_paired) {
    this->save_pairing_data();
  }

  if (runtime_paired) {
    ESP_LOGI(TAG, "Already paired");
  } else {
    ESP_LOGI(TAG, "Not paired — entering pairing mode");
  }

  this->command_queue_ = xQueueCreate(10, sizeof(NukiCommand));
  if (this->command_queue_ == nullptr) {
    ESP_LOGE(TAG, "Queue creation failed");
    this->mark_failed(LOG_STR("Command queue creation failed"));
    return;
  }

  // BLE task → Core 0 (NimBLE affinity, isolated from LwIP on Core 1)
  BaseType_t res =
      xTaskCreatePinnedToCore(NukiProLock::nuki_task_runner, "nukiBLE", 10240,
                              this, 5, &this->task_handle_, 0);

  if (res != pdPASS) {
    ESP_LOGE(TAG, "BLE task creation failed");
    this->mark_failed(LOG_STR("BLE task creation failed"));
    return;
  }

  this->publish_state(lock::LOCK_STATE_NONE);
  ESP_LOGI(TAG, "Ready — BLE on Core 0, keepalive=%s, poll=%ums",
           YESNO(this->keepalive_), this->poll_interval_ms_);
}

// ── Loop (Core 1) — event-driven, woken by Core 0 ────────────────────

void NukiProLock::loop() {
  if (this->state_updated_.exchange(false)) {
    this->publish_state(this->pending_state_.load());
  }
  this->disable_loop();
}

// ── Control (Core 1 → queue → Core 0) ─────────────────────────────────

void NukiProLock::control(const lock::LockCall &call) {
  NukiCommand cmd{NukiCommandType::NONE};
  if (call.get_state().has_value()) {
    auto s = *call.get_state();
    if (s == lock::LOCK_STATE_LOCKED)
      cmd.type = NukiCommandType::LOCK;
    if (s == lock::LOCK_STATE_UNLOCKED)
      cmd.type = NukiCommandType::UNLOCK;
  }
  if (cmd.type != NukiCommandType::NONE) {
    if (xQueueSend(this->command_queue_, &cmd, 0) != pdTRUE)
      ESP_LOGW(TAG, "Command queue full");
  }
}

void NukiProLock::open_latch() {
  NukiCommand cmd{NukiCommandType::UNLATCH};
  if (xQueueSend(this->command_queue_, &cmd, 0) != pdTRUE)
    ESP_LOGW(TAG, "Command queue full");
}

// ── notify() — BLE context (Core 0), atomics only ─────────────────────

void NukiProLock::notify(Nuki::EventType event_type) {
  switch (event_type) {
  case Nuki::EventType::KeyTurnerStatusUpdated:
    this->status_poll_requested_.store(true);
    break;
  case Nuki::EventType::ERROR_BAD_PIN:
    ESP_LOGE(TAG, "Invalid PIN reported by Nuki");
    break;
  case Nuki::EventType::BLE_ERROR_ON_DISCONNECT:
    ESP_LOGE(TAG, "BLE disconnect failure");
    break;
  default:
    break;
  }
}

// ── BLE Task (Core 0) — xQueueReceive hybrid engine ───────────────────

void NukiProLock::nuki_task_runner(void *arg) {
  static_cast<NukiProLock *>(arg)->nuki_task();
  vTaskDelete(nullptr);
}

void NukiProLock::nuki_task() {
  ESP_LOGI(TAG, "BLE task started on Core %d", xPortGetCoreID());

  const TickType_t poll_ticks = pdMS_TO_TICKS(this->poll_interval_ms_);
  uint8_t action_retries = 0;
  NukiLock::LockAction pending_action = NukiLock::LockAction::Lock;

  // 8s keepalive pulse — stays under Nuki 5.0 Pro's 10s idle disconnect
  const uint32_t KEEPALIVE_MS = 8000;
  uint32_t last_keepalive = millis();

  while (true) {
    this->scanner_.update();
    this->nuki_lock_->updateConnectionState();

    NukiCommand cmd;

    // ── Hybrid wait: instant command wake OR timeout → poll ────
    if (xQueueReceive(this->command_queue_, &cmd, poll_ticks) == pdTRUE) {

      switch (cmd.type) {
      case NukiCommandType::LOCK:
        ESP_LOGI(TAG, "→ LOCK");
        this->pending_state_.store(lock::LOCK_STATE_LOCKING);
        this->state_updated_.store(true);
        this->enable_loop_soon_any_context();
        if (this->execute_lock_action(NukiLock::LockAction::Lock)) {
          this->status_poll_requested_.store(true);
        } else {
          action_retries = MAX_ACTION_ATTEMPTS;
          pending_action = NukiLock::LockAction::Lock;
        }
        break;

      case NukiCommandType::UNLOCK:
        ESP_LOGI(TAG, "→ UNLOCK");
        this->pending_state_.store(lock::LOCK_STATE_UNLOCKING);
        this->state_updated_.store(true);
        this->enable_loop_soon_any_context();
        if (this->execute_lock_action(NukiLock::LockAction::Unlock)) {
          this->status_poll_requested_.store(true);
        } else {
          action_retries = MAX_ACTION_ATTEMPTS;
          pending_action = NukiLock::LockAction::Unlock;
        }
        break;

      case NukiCommandType::UNLATCH:
        ESP_LOGI(TAG, "→ UNLATCH");
        this->pending_state_.store(lock::LOCK_STATE_UNLOCKING);
        this->state_updated_.store(true);
        this->enable_loop_soon_any_context();
        if (this->execute_lock_action(NukiLock::LockAction::Unlatch)) {
          this->status_poll_requested_.store(true);
        } else {
          action_retries = MAX_ACTION_ATTEMPTS;
          pending_action = NukiLock::LockAction::Unlatch;
        }
        break;

      case NukiCommandType::LOCK_N_GO:
        ESP_LOGI(TAG, "→ LOCK_N_GO");
        this->pending_state_.store(lock::LOCK_STATE_LOCKING);
        this->state_updated_.store(true);
        this->enable_loop_soon_any_context();
        this->execute_lock_action(NukiLock::LockAction::LockNgo);
        this->status_poll_requested_.store(true);
        break;

      case NukiCommandType::PAIR:
        this->do_pair();
        break;

      case NukiCommandType::UNPAIR:
        ESP_LOGI(TAG, "→ UNPAIR");
        this->nuki_lock_->unPairNuki();
        this->paired_.store(false);
        this->save_pairing_data();
        this->pending_state_.store(lock::LOCK_STATE_NONE);
        this->state_updated_.store(true);
        this->enable_loop_soon_any_context();
        break;

      case NukiCommandType::NONE:
        break;
      }

    } else {
      // ── Timeout: aggressive poll / retry / pair / keepalive ─

      if (action_retries > 0) {
        action_retries--;
        ESP_LOGD(TAG, "Retrying (%d left)", action_retries);
        if (this->execute_lock_action(pending_action)) {
          action_retries = 0;
          this->status_poll_requested_.store(true);
        }
        continue;
      }

      if (!this->paired_.load() && this->pairing_mode_.load()) {
        this->do_pair();
        continue;
      }

      if (this->paired_.load()) {
        if (this->status_poll_requested_.exchange(false)) {
          this->do_status_poll();
          last_keepalive = millis();
        } else if (this->keepalive_ &&
                   (millis() - last_keepalive > KEEPALIVE_MS)) {
          this->do_status_poll();
          last_keepalive = millis();
        }
      }
    }
  }
}

// ── BLE operations (Core 0) ───────────────────────────────────────────

bool NukiProLock::execute_lock_action(NukiLock::LockAction action) {
  if (!this->nuki_lock_->isPairedWithLock()) {
    ESP_LOGE(TAG, "Not paired");
    return false;
  }
  Nuki::CmdResult result = this->nuki_lock_->lockAction(action);
  char buf[30] = {0}, act[30] = {0};
  NukiLock::cmdResultToString(result, buf);
  NukiLock::lockactionToString(action, act);
  if (result == Nuki::CmdResult::Success) {
    ESP_LOGI(TAG, "%s → %s", act, buf);
    this->connected_.store(true);
    return true;
  }
  ESP_LOGE(TAG, "%s → %s", act, buf);
  return false;
}

void NukiProLock::do_status_poll() {
  Nuki::CmdResult r =
      this->nuki_lock_->requestKeyTurnerState(&this->key_turner_state_);
  if (r == Nuki::CmdResult::Success) {
    this->connected_.store(true);
    auto new_state =
        this->nuki_to_esphome_state(this->key_turner_state_.lockState);
    this->pending_state_.store(new_state);
    this->state_updated_.store(true);
    this->enable_loop_soon_any_context();

    if (this->key_turner_state_.lockState == NukiLock::LockState::Locking ||
        this->key_turner_state_.lockState == NukiLock::LockState::Unlocking) {
      this->status_poll_requested_.store(true);
    }
  } else {
    ESP_LOGW(TAG, "Poll failed");
    this->status_poll_requested_.store(true);
  }
}

void NukiProLock::do_pair() {
  ESP_LOGI(TAG, "Pairing (Ultra/5th Gen)...");
  auto pr = this->nuki_lock_->pairNuki(Nuki::AuthorizationIdType::App);
  if (pr == Nuki::PairingResult::Success) {
    ESP_LOGI(TAG, "Paired!");
    this->paired_.store(true);
    this->pairing_mode_.store(false);
    if (this->pin_ != 0) {
      this->nuki_lock_->saveUltraPincode(this->pin_);
    }
    this->save_pairing_data();
    this->status_poll_requested_.store(true);
  } else {
    ESP_LOGW(TAG, "Pairing failed (%d)", (int)pr);
  }
}

void NukiProLock::dump_config() {
  ESP_LOGCONFIG(TAG, "Nuki 5.0 Pro:");
  ESP_LOGCONFIG(TAG, "  PIN: %06u", this->pin_);
  ESP_LOGCONFIG(TAG, "  Nuki ID: %u", this->nuki_id_);
  ESP_LOGCONFIG(TAG, "  Poll: %u ms", this->poll_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Keepalive: %s", YESNO(this->keepalive_));
  ESP_LOGCONFIG(TAG, "  Paired: %s", YESNO(this->paired_.load()));
  ESP_LOGCONFIG(TAG, "  BLE Core: 0");
  ESP_LOGCONFIG(TAG, "  LwIP Core: 1 (isolated)");
}

} // namespace nuki_pro
} // namespace esphome
