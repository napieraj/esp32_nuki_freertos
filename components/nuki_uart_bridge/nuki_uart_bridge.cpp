#include "nuki_uart_bridge.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <map>
#include <string>

namespace esphome {
namespace nuki_uart_bridge {

// ── Name tables ───────────────────────────────────────────────────────

lock::LockState
NukiUartBridgeLock::nuki_to_esphome_state(uint8_t nuki_lock_state) {
  // Nuki API v2.3.1 p.31
  switch (nuki_lock_state) {
  case NUKI_LOCK_STATE_LOCKED:
    return lock::LOCK_STATE_LOCKED;
  case NUKI_LOCK_STATE_UNLOCKING:
  case NUKI_LOCK_STATE_UNLATCHING:
    return lock::LOCK_STATE_UNLOCKING;
  case NUKI_LOCK_STATE_UNLOCKED:
  case NUKI_LOCK_STATE_UNLATCHED:
  case NUKI_LOCK_STATE_UNLOCKED_LNG:
    return lock::LOCK_STATE_UNLOCKED;
  case NUKI_LOCK_STATE_LOCKING:
    return lock::LOCK_STATE_LOCKING;
  case NUKI_LOCK_STATE_MOTOR_BLOCKED:
    return lock::LOCK_STATE_JAMMED;
  default: // uncalibrated, calibration, boot run, undefined
    return lock::LOCK_STATE_NONE;
  }
}

const char *NukiUartBridgeLock::uart_error_name(uint8_t code) {
  switch (code) {
  case NUKI_UART_ERR_PAIRING_BUSY:
    return "PAIRING_BUSY";
  case NUKI_UART_ERR_NOT_PAIRED:
    return "NOT_PAIRED";
  case NUKI_UART_ERR_PAIRING_FAILED:
    return "PAIRING_FAILED";
  case NUKI_UART_ERR_SCAN_START_FAILED:
    return "SCAN_START_FAILED";
  case NUKI_UART_ERR_METRICS_UNAVAILABLE:
    return "METRICS_UNAVAILABLE";
  case NUKI_UART_ERR_PIN_REQUIRED:
    return "PIN_REQUIRED";
  case NUKI_UART_ERR_PAIRING_WINDOW_CLOSED:
    return "PAIRING_WINDOW_CLOSED";
  case NUKI_UART_ERR_ENCRYPT_FAILED:
    return "ENCRYPT_FAILED";
  case NUKI_UART_ERR_DECRYPT_FAILED:
    return "DECRYPT_FAILED";
  case NUKI_UART_ERR_TIMEOUT:
    return "TIMEOUT";
  case NUKI_UART_ERR_QUEUE_FULL:
    return "QUEUE_FULL";
  case NUKI_UART_ERR_NOT_CONNECTED:
    return "NOT_CONNECTED";
  case NUKI_UART_ERR_LOCK_BUSY:
    return "LOCK_BUSY";
  case NUKI_UART_ERR_INVALID_PAYLOAD:
    return "INVALID_PAYLOAD";
  case NUKI_UART_ERR_UNSUPPORTED:
    return "UNSUPPORTED";
  case NUKI_UART_ERR_ANTI_REPLAY:
    return "ANTI_REPLAY";
  case NUKI_UART_ERR_UART_AUTH:
    return "UART_AUTH";
  case NUKI_UART_ERR_LOCK_ERROR:
    return "LOCK_ERROR";
  case NUKI_UART_ERR_INTERNAL:
    return "INTERNAL";
  case NUKI_UART_ERR_UNKNOWN_CMD:
    return "UNKNOWN_CMD";
  default:
    return "?";
  }
}

const char *NukiUartBridgeLock::nuki_error_name(uint8_t code) {
  // Nuki API v2.3.1 pp.74-79 (subset relevant to lock actions)
  switch (code) {
  case 0x10:
    return "P_ERROR_NOT_PAIRING";
  case 0x11:
    return "P_ERROR_BAD_AUTHENTICATOR";
  case 0x12:
    return "P_ERROR_BAD_PARAMETER";
  case 0x13:
    return "P_ERROR_MAX_USER";
  case 0x20:
    return "K_ERROR_NOT_AUTHORIZED";
  case 0x21:
    return "K_ERROR_BAD_PIN";
  case 0x22:
    return "K_ERROR_BAD_NONCE";
  case 0x23:
    return "K_ERROR_BAD_PARAMETER";
  case 0x24:
    return "K_ERROR_INVALID_AUTH_ID";
  case 0x25:
    return "K_ERROR_DISABLED";
  case 0x26:
    return "K_ERROR_REMOTE_NOT_ALLOWED";
  case 0x27:
    return "K_ERROR_TIME_NOT_ALLOWED";
  case 0x28:
    return "K_ERROR_TOO_MANY_PIN_ATTEMPTS";
  case 0x40:
    return "K_ERROR_AUTO_UNLOCK_TOO_RECENT";
  case 0x41:
    return "K_ERROR_POSITION_UNKNOWN";
  case 0x42:
    return "K_ERROR_MOTOR_BLOCKED";
  case 0x43:
    return "K_ERROR_CLUTCH_FAILURE";
  case 0x44:
    return "K_ERROR_MOTOR_TIMEOUT";
  case 0x45:
    return "K_ERROR_BUSY";
  case 0x46:
    return "K_ERROR_CANCELED";
  case 0x47:
    return "K_ERROR_NOT_CALIBRATED";
  case 0x48:
    return "K_ERROR_MOTOR_POSITION_LIMIT";
  case 0x49:
    return "K_ERROR_MOTOR_LOW_VOLTAGE";
  case 0x4A:
    return "K_ERROR_MOTOR_POWER_FAILURE";
  case 0x4B:
    return "K_ERROR_CLUTCH_POWER_FAILURE";
  case 0x4C:
    return "K_ERROR_VOLTAGE_TOO_LOW";
  case 0x4D:
    return "K_ERROR_FIRMWARE_UPDATE_NEEDED";
  case 0xFD:
    return "ERROR_BAD_CRC";
  case 0xFE:
    return "ERROR_BAD_LENGTH";
  case 0xFF:
    return "ERROR_UNKNOWN";
  default:
    return "?";
  }
}

const char *NukiUartBridgeLock::conn_state_name(uint8_t state) {
  switch (state) {
  case NUKI_UART_CONN_IDLE:
    return "IDLE";
  case NUKI_UART_CONN_SCANNING:
    return "SCANNING";
  case NUKI_UART_CONN_CONNECTING:
    return "CONNECTING";
  case NUKI_UART_CONN_DISCOVERING:
    return "DISCOVERING";
  case NUKI_UART_CONN_SUBSCRIBING:
    return "SUBSCRIBING";
  case NUKI_UART_CONN_CONNECTED:
    return "CONNECTED";
  case NUKI_UART_CONN_BACKOFF_WAIT:
    return "BACKOFF_WAIT";
  default:
    return "?";
  }
}

const char *NukiUartBridgeLock::device_type_name(uint8_t t) {
  switch (t) {
  case NUKI_UART_DEVICE_AUTO:
    return "auto";
  case NUKI_UART_DEVICE_CLASSIC:
    return "classic";
  case NUKI_UART_DEVICE_ULTRA:
    return "ultra";
  case NUKI_UART_DEVICE_OPENER:
    return "opener";
  default:
    return "?";
  }
}

// ── Setup / config ────────────────────────────────────────────────────

void NukiUartBridgeLock::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Nuki UART bridge host...");
  this->rx_cobs_len_ = 0;
  this->rx_discard_ = true; // sync on the first 0x00
  this->publish_state(lock::LOCK_STATE_NONE);
  if (this->connected_sensor_ != nullptr) {
    this->connected_sensor_->publish_state(false);
  }
  this->last_rx_ms_ = millis();

  if (this->secure_link_ != SecureLinkMode::OFF) {
    nuki_seclink_static_t self_key;
    uint8_t peer_pk[NUKI_SECLINK_KEY_LEN];
    this->keys_.init(this->get_object_id_hash());
    this->sec_available_ = this->keys_.load_or_create_static(&self_key);
    const bool have_peer =
        this->sec_available_ && this->keys_.load_peer_pk(peer_pk);
    nuki_seclink_init(&this->sec_, NUKI_SECLINK_INITIATOR, &self_key,
                      have_peer ? peer_pk : nullptr);
    volatile uint8_t *w = reinterpret_cast<volatile uint8_t *>(&self_key);
    for (size_t i = 0; i < sizeof(self_key); i++) {
      w[i] = 0;
    }
    if (!this->sec_available_) {
      ESP_LOGE(TAG, "Secure link unavailable (key store failed)");
    } else {
      ESP_LOGI(TAG, "Secure link: host key ready, bridge key %s",
               have_peer ? "stored" : "not paired yet");
    }
  }

  this->send_hello_();

#ifdef USE_API
#ifdef USE_API_CUSTOM_SERVICES
  this->register_service(&NukiUartBridgeLock::print_keypad_entries,
                         "print_keypad_entries");
  this->register_service(&NukiUartBridgeLock::add_keypad_entry,
                         "add_keypad_entry", {"name", "code"});
  this->register_service(&NukiUartBridgeLock::update_keypad_entry,
                         "update_keypad_entry",
                         {"id", "name", "code", "enabled"});
  this->register_service(&NukiUartBridgeLock::delete_keypad_entry,
                         "delete_keypad_entry", {"id"});
  this->register_service(&NukiUartBridgeLock::request_event_logs,
                         "request_event_logs", {"count"});
  this->register_service(&NukiUartBridgeLock::pair_host, "pair_host");
#else
  ESP_LOGW(TAG, "Keypad/event-log services need 'api: custom_services: true'");
#endif
#ifndef USE_API_HOMEASSISTANT_SERVICES
  if (this->send_events_) {
    ESP_LOGW(TAG, "event '%s' needs 'api: homeassistant_services: true'",
             this->event_);
  }
#endif
#endif
}

void NukiUartBridgeLock::dump_config() {
  ESP_LOGCONFIG(TAG, "Nuki UART bridge:");
  LOG_LOCK("  ", "Lock", this);
  ESP_LOGCONFIG(TAG, "  PIN: %s", this->pin_ != 0 ? "set" : "none");
  ESP_LOGCONFIG(TAG, "  Device type: %s", device_type_name(this->device_type_));
  ESP_LOGCONFIG(TAG, "  Pair as: %s",
                this->id_type_ == NUKI_UART_ID_TYPE_BRIDGE ? "bridge" : "app");
  ESP_LOGCONFIG(TAG, "  App ID: %" PRIu32 " (0 = bridge default)",
                this->app_id_);
  ESP_LOGCONFIG(TAG, "  Link profile: %s",
                this->link_profile_ == NUKI_UART_LINK_PROFILE_ARMED ? "armed"
                                                                    : "eco");
  ESP_LOGCONFIG(TAG, "  Poll interval: %" PRIu32 " ms",
                this->poll_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Auto pair: %s", YESNO(this->auto_pair_));
  ESP_LOGCONFIG(TAG, "  Bridge state poll: %u s (0 = bridge default)",
                this->state_poll_s_);
  ESP_LOGCONFIG(TAG, "  Event: %s", this->send_events_ ? this->event_ : "off");
  ESP_LOGCONFIG(TAG, "  Event log fetch: %u newest entries after each action",
                this->event_log_count_);
  ESP_LOGCONFIG(TAG, "  Secure link: %s (host key %s, bridge key %s, %s)",
                this->secure_link_ == SecureLinkMode::OFF    ? "off"
                : this->secure_link_ == SecureLinkMode::AUTO ? "auto"
                                                             : "required",
                this->sec_available_ ? "ok" : "MISSING",
                this->sec_.state >= NUKI_SECLINK_PAIRED ? "stored" : "none",
                nuki_seclink_state_name(this->sec_.state));
  ESP_LOGCONFIG(TAG, "  Link: %s (framing v%u)",
                this->link_ == LinkState::READY ? "ready" : "waiting for HELLO",
                this->version_);
  ESP_LOGCONFIG(TAG, "  Paired: %s  Connected: %s", YESNO(this->paired_),
                YESNO(this->connected_));
  {
    const uint32_t baud = this->parent_->get_baud_rate();
    ESP_LOGCONFIG(TAG, "  UART: %" PRIu32 " baud 8N1", baud);
    if (baud != 921600 && baud != 115200) {
      ESP_LOGW(TAG, "  Bridge expects 921600 (default) or 115200 8N1");
    }
  }
  LOG_BINARY_SENSOR("  ", "Connected", this->connected_sensor_);
  LOG_SENSOR("  ", "RSSI", this->rssi_sensor_);
  LOG_TEXT_SENSOR("  ", "Diagnostics", this->diagnostics_sensor_);
  LOG_TEXT_SENSOR("  ", "Last unlock user", this->last_unlock_user_sensor_);
  LOG_TEXT_SENSOR("  ", "Last lock action", this->last_lock_action_sensor_);
  LOG_TEXT_SENSOR("  ", "Last lock action trigger",
                  this->last_lock_action_trigger_sensor_);
  LOG_TEXT_SENSOR("  ", "Door sensor state", this->door_sensor_state_sensor_);
  LOG_BINARY_SENSOR("  ", "Door sensor", this->door_sensor_);
  LOG_BINARY_SENSOR("  ", "Tamper", this->tamper_sensor_);
}

// ── Main loop (Core 1) — bounded, never blocks ────────────────────────

void NukiUartBridgeLock::loop() {
  const uint32_t now = millis();

  this->drain_rx_();

  if (this->link_ == LinkState::HELLO_PENDING) {
    if (now - this->hello_sent_ms_ >= this->hello_retry_ms_) {
      this->hello_retry_ms_ =
          std::min<uint32_t>(this->hello_retry_ms_ * 2, HELLO_RETRY_MAX_MS);
      this->send_hello_();
    }
    return;
  }

  if (now - this->last_rx_ms_ > LINK_LOST_MS) {
    this->on_link_lost_();
    return;
  }

  if (this->sec_in_use_() && this->sec_.state == NUKI_SECLINK_HS_SENT &&
      now - this->hs_sent_ms_ >= HS_RETRY_MS * this->hs_attempts_) {
    if (this->hs_attempts_ < HS_MAX_ATTEMPTS) {
      ESP_LOGW(TAG, "No HS_RESP from the bridge — resending HS_INIT");
      this->start_handshake_();
    } else {
      ESP_LOGE(TAG,
               "Secure handshake failed %u times; waiting for the next "
               "HELLO (bridge reset) or pair_host()",
               this->hs_attempts_);
      nuki_seclink_end_session(&this->sec_); // -> PAIRED, stops the retries
    }
  }
  if (this->host_pairing_ &&
      now - this->host_pair_sent_ms_ > HOST_PAIR_TIMEOUT_MS) {
    ESP_LOGE(TAG, "pair_host: no PAIR reply — is the bridge's pairing window "
                  "open? (first boot, button, or reboot after UNPAIR_HOST)");
    this->host_pairing_ = false;
  }

  if (now - this->last_ping_ms_ >= PING_INTERVAL_MS) {
    this->send_ping();
  }

  if (this->paired_ && this->connected_ && this->poll_interval_ms_ > 0 &&
      now - this->last_poll_ms_ >= this->poll_interval_ms_) {
    this->request_lock_state();
  }

  if (this->pairing_ && now - this->pair_sent_ms_ > PAIR_TIMEOUT_MS) {
    ESP_LOGW(TAG, "Pairing timed out on the host side");
    this->pairing_ = false;
  }

  if (this->stream_cmd_ != 0 && now - this->stream_last_ms_ > STREAM_IDLE_MS) {
    this->end_stream_("idle timeout");
  }

  this->expire_pending_(now);
}

// ── Lock control (fast path: write the frame right here) ──────────────

void NukiUartBridgeLock::control(const lock::LockCall &call) {
  if (!call.get_state().has_value()) {
    return;
  }
  switch (*call.get_state()) {
  case lock::LOCK_STATE_LOCKED:
    this->send_action_(NUKI_UART_CMD_LOCK, lock::LOCK_STATE_LOCKING);
    break;
  case lock::LOCK_STATE_UNLOCKED:
    this->send_action_(NUKI_UART_CMD_UNLOCK, lock::LOCK_STATE_UNLOCKING);
    break;
  default:
    break;
  }
}

void NukiUartBridgeLock::open_latch() {
  this->send_action_(NUKI_UART_CMD_UNLATCH, lock::LOCK_STATE_UNLOCKING);
}

bool NukiUartBridgeLock::send_action_(uint8_t cmd, lock::LockState optimistic) {
  if (this->link_ != LinkState::READY) {
    ESP_LOGW(TAG, "Action 0x%02X dropped: bridge link not ready", cmd);
    return false;
  }
  if (!this->paired_) {
    ESP_LOGW(TAG, "Action 0x%02X sent while bridge reports unpaired", cmd);
  }
  uint16_t seq = 0;
  if (!this->send_cmd_(cmd, nullptr, 0, &seq)) {
    return false;
  }
  this->action_seq_ = seq;
  this->action_cmd_ = cmd;
  this->action_sent_ms_ = millis();
  this->action_accepted_ms_ = 0;
  this->publish_state(optimistic);
  ESP_LOGI(TAG, "-> action 0x%02X seq=%u", cmd, seq);
  return true;
}

// ── Operator actions ──────────────────────────────────────────────────

void NukiUartBridgeLock::pair() {
  if (this->link_ != LinkState::READY) {
    ESP_LOGW(TAG, "PAIR: bridge link not ready");
    return;
  }
  if (this->pairing_) {
    ESP_LOGW(TAG, "PAIR: already pairing");
    return;
  }
  uint8_t payload[10];
  size_t n = nuki_uart_build_pair_payload(
      payload, this->device_type_, this->pin_, this->id_type_, this->app_id_);
  uint16_t seq = 0;
  if (this->send_cmd_(NUKI_UART_CMD_PAIR, payload, n, &seq)) {
    this->pairing_ = true;
    this->pair_attempted_ = true;
    this->pair_seq_ = seq;
    this->pair_sent_ms_ = millis();
    ESP_LOGI(TAG,
             "-> PAIR seq=%u (device=%s, pin=%s, as=%s, app_id=%" PRIu32
             ") — put the lock in pairing mode",
             seq, device_type_name(this->device_type_),
             this->pin_ != 0 ? "yes" : "no",
             this->id_type_ == NUKI_UART_ID_TYPE_BRIDGE ? "bridge" : "app",
             this->app_id_);
  }
}

void NukiUartBridgeLock::unpair() {
  if (this->link_ != LinkState::READY) {
    ESP_LOGW(TAG, "UNPAIR: bridge link not ready");
    return;
  }
  uint8_t payload[4];
  size_t n = 0;
  if (this->pin_ != 0) {
    nuki_uart_put_u32(payload, this->pin_);
    n = 4;
  }
  uint16_t seq = 0;
  if (this->send_cmd_(NUKI_UART_CMD_UNPAIR, payload, n, &seq)) {
    ESP_LOGW(TAG, "-> UNPAIR seq=%u (%s)", seq,
             n ? "with PIN: authorization removed from lock"
               : "no PIN: dead entry stays in the lock");
  }
}

void NukiUartBridgeLock::request_diagnostics() {
  this->send_cmd_(NUKI_UART_CMD_REQ_DIAGNOSTICS, nullptr, 0);
}

void NukiUartBridgeLock::request_lock_state() {
  this->last_poll_ms_ = millis();
  this->send_cmd_(NUKI_UART_CMD_REQ_LOCK_STATE, nullptr, 0);
}

void NukiUartBridgeLock::send_ping() {
  this->last_ping_ms_ = millis();
  this->send_cmd_(NUKI_UART_CMD_PING, nullptr, 0);
}

void NukiUartBridgeLock::set_runtime_link_profile(uint8_t profile) {
  this->link_profile_ = profile;
  this->send_cmd_(NUKI_UART_CMD_SET_LINK_PROFILE, &profile, 1);
}

void NukiUartBridgeLock::set_runtime_state_poll(uint16_t seconds) {
  this->state_poll_s_ = seconds;
  if (seconds == 0) {
    return; // keep the bridge default
  }
  uint8_t payload[2];
  size_t n = nuki_uart_build_set_state_poll(payload, seconds);
  this->send_cmd_(NUKI_UART_CMD_SET_STATE_POLL, payload, n);
}

// ── Payload commands: event log, keypad, authorizations ───────────────
//
// All of these are "fields minus nK, PIN last" (bridge docs §7).  The lock
// answers a bulk request with N entry frames (0x87/0x88/0x89) followed by
// Status COMPLETE; the bridge may already have closed the request when the
// leading *Count frame arrived, so entries are accepted with the request
// SEQ or with SEQ 0 while a stream is open.

uint8_t NukiUartBridgeLock::pin_device_type_() const {
  // Width follows what the bridge learned about the lock (diagnostics);
  // the configured device_type is the fallback before the first report.
  if (this->diag_.valid && this->diag_.device_type != NUKI_UART_DEVICE_AUTO) {
    return this->diag_.device_type;
  }
  return this->device_type_;
}

bool NukiUartBridgeLock::pin_ready_(const char *what) const {
  if (this->link_ != LinkState::READY) {
    ESP_LOGW(TAG, "%s: bridge link not ready", what);
    return false;
  }
  if (!this->paired_) {
    ESP_LOGW(TAG, "%s: bridge is not paired", what);
    return false;
  }
  if (this->pin_ == 0) {
    ESP_LOGW(TAG, "%s: security_pin is not configured", what);
    return false;
  }
  const uint8_t dt = this->pin_device_type_();
  if (dt == NUKI_UART_DEVICE_AUTO) {
    ESP_LOGW(TAG, "%s: device type unknown yet (waiting for diagnostics)",
             what);
    return false;
  }
  if (dt != NUKI_UART_DEVICE_ULTRA && this->pin_ > 0xFFFF) {
    ESP_LOGW(TAG, "%s: PIN does not fit the 4-digit container of a gen 1-4",
             what);
    return false;
  }
  return true;
}

bool NukiUartBridgeLock::send_payload_cmd_(uint8_t cmd, const uint8_t *data,
                                           size_t len, uint16_t *seq_out) {
  if (len > NUKI_UART_PAYLOAD_MAX) {
    ESP_LOGE(TAG, "cmd 0x%02X payload %u > %u", cmd, (unsigned)len,
             (unsigned)NUKI_UART_PAYLOAD_MAX);
    return false;
  }
  return this->send_cmd_(cmd, data, len, seq_out);
}

void NukiUartBridgeLock::begin_stream_(uint8_t cmd, uint16_t seq) {
  if (this->stream_cmd_ != 0) {
    this->end_stream_("superseded");
  }
  this->stream_cmd_ = cmd;
  this->stream_seq_ = seq;
  this->stream_entries_ = 0;
  this->stream_last_ms_ = millis();
}

void NukiUartBridgeLock::end_stream_(const char *why) {
  if (this->stream_cmd_ == 0) {
    return;
  }
  ESP_LOGD(TAG, "stream 0x%02X seq=%u done: %u entries (%s)", this->stream_cmd_,
           this->stream_seq_, this->stream_entries_, why);
  if (this->stream_cmd_ == NUKI_UART_CMD_REQ_AUTH_ENTRIES) {
    this->auth_fetched_ = true;
    this->auth_fetched_ms_ = millis();
  } else if (this->stream_cmd_ == NUKI_UART_CMD_REQ_LOG_ENTRIES) {
    this->log_batch_floor_ = this->last_log_index_;
  }
  this->stream_cmd_ = 0;
  this->stream_seq_ = 0;
}

void NukiUartBridgeLock::request_event_logs(int32_t count) {
  if (!this->pin_ready_("request_event_logs")) {
    return;
  }
  if (count < 1) {
    count = 1;
  }
  if (count > LOG_REQUEST_MAX) {
    count = LOG_REQUEST_MAX;
  }
  // Newest first, no Log Entry Count frame (total_count = 0) so the bridge
  // keeps the request open until Status COMPLETE.
  uint8_t payload[16];
  size_t n = nuki_uart_build_req_log_entries(
      payload, 0, (uint16_t)count, NUKI_LOG_SORT_DESCENDING, 0, this->pin_,
      this->pin_device_type_());
  uint16_t seq = 0;
  if (this->send_payload_cmd_(NUKI_UART_CMD_REQ_LOG_ENTRIES, payload, n,
                              &seq)) {
    this->begin_stream_(NUKI_UART_CMD_REQ_LOG_ENTRIES, seq);
    ESP_LOGD(TAG,
             "-> REQ_LOG_ENTRIES newest %d seq=%u (floor index %" PRIu32 ")",
             (int)count, seq, this->log_batch_floor_);
  }
}

// Names for the event log: once per pairing, refreshed every 6 h.  Needs the
// device type from diagnostics for the PIN width, so it is also retried from
// handle_diagnostics_().
void NukiUartBridgeLock::maybe_request_auth_entries_() {
  if (!this->want_logs_() || this->pin_ == 0 || !this->paired_ ||
      !this->connected_ || !this->diag_.valid || this->stream_cmd_ != 0) {
    return;
  }
  if (this->auth_fetched_ &&
      millis() - this->auth_fetched_ms_ < AUTH_REFRESH_MS) {
    return;
  }
  this->request_auth_entries();
}

void NukiUartBridgeLock::request_auth_entries() {
  if (!this->pin_ready_("request_auth_entries")) {
    return;
  }
  uint8_t payload[8];
  size_t n = nuki_uart_build_req_auth_entries(
      payload, 0, AUTH_REQUEST_COUNT, this->pin_, this->pin_device_type_());
  uint16_t seq = 0;
  if (this->send_payload_cmd_(NUKI_UART_CMD_REQ_AUTH_ENTRIES, payload, n,
                              &seq)) {
    this->begin_stream_(NUKI_UART_CMD_REQ_AUTH_ENTRIES, seq);
    ESP_LOGD(TAG, "-> REQ_AUTH_ENTRIES seq=%u", seq);
  }
}

void NukiUartBridgeLock::print_keypad_entries() {
  if (!this->pin_ready_("print_keypad_entries")) {
    return;
  }
  uint8_t payload[8];
  size_t n = nuki_uart_build_req_keypad_codes(
      payload, 0, KEYPAD_REQUEST_COUNT, this->pin_, this->pin_device_type_());
  uint16_t seq = 0;
  if (this->send_payload_cmd_(NUKI_UART_CMD_REQ_KEYPAD_CODES, payload, n,
                              &seq)) {
    this->begin_stream_(NUKI_UART_CMD_REQ_KEYPAD_CODES, seq);
    ESP_LOGI(TAG, "-> REQ_KEYPAD_CODES seq=%u", seq);
  }
}

bool NukiUartBridgeLock::valid_keypad_name_(const std::string &name) {
  if (name.empty() || name == "--" || name.size() > NUKI_KEYPAD_NAME_LEN) {
    ESP_LOGE(TAG, "Keypad name '%s' is invalid (1..%u characters)",
             name.c_str(), (unsigned)NUKI_KEYPAD_NAME_LEN);
    return false;
  }
  return true;
}

bool NukiUartBridgeLock::valid_keypad_code_(int32_t code) {
  // Nuki keypad codes: 6 digits, digit 0 not allowed.
  bool ok = code >= 100000 && code <= 999999;
  for (int32_t c = code; ok && c > 0; c /= 10) {
    if (c % 10 == 0) {
      ok = false;
    }
  }
  if (!ok) {
    ESP_LOGE(TAG, "Keypad code is invalid: must be 6 digits without 0");
  }
  return ok;
}

bool NukiUartBridgeLock::valid_keypad_id_(int32_t id) {
  if (id < 1 || id > 0xFFFF) {
    ESP_LOGE(TAG, "Keypad code id %" PRId32 " is invalid", id);
    return false;
  }
  return true;
}

void NukiUartBridgeLock::add_keypad_entry(std::string name, int32_t code) {
  if (!this->pin_ready_("add_keypad_entry")) {
    return;
  }
  if (!valid_keypad_name_(name) || !valid_keypad_code_(code)) {
    return;
  }
  uint8_t payload[NUKI_UART_PAYLOAD_MAX];
  size_t n =
      nuki_uart_build_add_keypad(payload, (uint32_t)code, name.c_str(), nullptr,
                                 this->pin_, this->pin_device_type_());
  uint16_t seq = 0;
  if (this->send_payload_cmd_(NUKI_UART_CMD_ADD_KEYPAD, payload, n, &seq)) {
    ESP_LOGI(TAG, "-> ADD_KEYPAD '%s' seq=%u", name.c_str(), seq);
  }
}

void NukiUartBridgeLock::update_keypad_entry(int32_t id, std::string name,
                                             int32_t code, bool enabled) {
  if (!this->pin_ready_("update_keypad_entry")) {
    return;
  }
  if (!valid_keypad_id_(id) || !valid_keypad_name_(name) ||
      !valid_keypad_code_(code)) {
    return;
  }
  uint8_t payload[NUKI_UART_PAYLOAD_MAX];
  size_t n = nuki_uart_build_update_keypad(
      payload, (uint16_t)id, (uint32_t)code, name.c_str(), enabled ? 1 : 0,
      nullptr, this->pin_, this->pin_device_type_());
  uint16_t seq = 0;
  if (this->send_payload_cmd_(NUKI_UART_CMD_UPDATE_KEYPAD, payload, n, &seq)) {
    ESP_LOGI(TAG, "-> UPDATE_KEYPAD #%" PRId32 " '%s' %s seq=%u", id,
             name.c_str(), enabled ? "enabled" : "disabled", seq);
  }
}

void NukiUartBridgeLock::delete_keypad_entry(int32_t id) {
  if (!this->pin_ready_("delete_keypad_entry")) {
    return;
  }
  if (!valid_keypad_id_(id)) {
    return;
  }
  uint8_t payload[8];
  size_t n = nuki_uart_build_remove_keypad(payload, (uint16_t)id, this->pin_,
                                           this->pin_device_type_());
  uint16_t seq = 0;
  if (this->send_payload_cmd_(NUKI_UART_CMD_REMOVE_KEYPAD, payload, n, &seq)) {
    ESP_LOGI(TAG, "-> REMOVE_KEYPAD #%" PRId32 " seq=%u", id, seq);
  }
}

void NukiUartBridgeLock::schedule_log_poll_() {
  if (!this->want_logs_() || this->pin_ == 0 || this->log_poll_scheduled_) {
    return;
  }
  this->log_poll_scheduled_ = true;
  // Let the lock finish writing its log entry before asking for it.
  this->set_timeout("nuki_log_poll", LOG_POLL_DELAY_MS, [this]() {
    this->log_poll_scheduled_ = false;
    if (this->connected_) {
      this->request_event_logs(this->event_log_count_);
    }
  });
}

const char *NukiUartBridgeLock::get_auth_name(uint32_t auth_id) const {
  for (const auto &a : this->auth_names_) {
    if (a.used && a.auth_id == auth_id) {
      return a.name;
    }
  }
  return nullptr;
}

void NukiUartBridgeLock::remember_auth_name_(uint32_t auth_id,
                                             const char *name) {
  AuthName *slot = nullptr;
  for (auto &a : this->auth_names_) {
    if (a.used && a.auth_id == auth_id) {
      slot = &a;
      break;
    }
    if (!a.used && slot == nullptr) {
      slot = &a;
    }
  }
  if (slot == nullptr) {
    ESP_LOGD(TAG, "auth name table full, dropping id %" PRIu32, auth_id);
    return;
  }
  slot->used = true;
  slot->auth_id = auth_id;
  strncpy(slot->name, name, sizeof(slot->name) - 1);
  slot->name[sizeof(slot->name) - 1] = '\0';
}

void NukiUartBridgeLock::publish_last_unlock_user_(const char *name) {
  if (strcmp(this->last_unlock_user_, name) == 0) {
    return;
  }
  strncpy(this->last_unlock_user_, name, sizeof(this->last_unlock_user_) - 1);
  this->last_unlock_user_[sizeof(this->last_unlock_user_) - 1] = '\0';
  if (this->last_unlock_user_sensor_ != nullptr) {
    this->last_unlock_user_sensor_->publish_state(this->last_unlock_user_);
  }
}

void NukiUartBridgeLock::process_log_entry_(const nuki_log_entry_t &e) {
  // Name resolution as the BLE component does it: the authorization list
  // wins, then the name inside the entry, then "Manual" (empty = the lock
  // itself, e.g. a key turn).
  const char *auth_name = this->get_auth_name(e.auth_id);
  if (auth_name == nullptr) {
    auth_name = e.name[0] != '\0' ? e.name : "Manual";
  }

  ESP_LOGD(TAG,
           "log #%" PRIu32 " %04u-%02u-%02u %02u:%02u:%02u auth=%" PRIu32
           " '%s' type=%s data=%02X %02X %02X %02X %02X (%u)",
           e.index, e.ts.year, e.ts.month, e.ts.day, e.ts.hour, e.ts.minute,
           e.ts.second, e.auth_id, auth_name, nuki_log_type_name(e.type),
           e.data[0], e.data[1], e.data[2], e.data[3], e.data[4], e.data_len);

  if ((e.type == NUKI_LOG_TYPE_LOCK_ACTION ||
       e.type == NUKI_LOG_TYPE_KEYPAD_ACTION) &&
      e.index > this->last_user_index_) {
    this->last_user_index_ = e.index;
    this->publish_last_unlock_user_(auth_name);
  }

  if (e.index > this->log_batch_floor_) {
    if (e.index > this->last_log_index_) {
      this->last_log_index_ = e.index;
    }
    this->fire_log_event_(e, auth_name);
    this->event_log_callback_.call(e);
  }
}

void NukiUartBridgeLock::fire_log_event_(const nuki_log_entry_t &e,
                                         const char *auth_name) {
  if (!this->send_events_) {
    return;
  }
#if defined(USE_API) && defined(USE_API_HOMEASSISTANT_SERVICES)
  char num[32];
  std::map<std::string, std::string> data;
  snprintf(num, sizeof(num), "%" PRIu32, e.index);
  data["index"] = num;
  snprintf(num, sizeof(num), "%" PRIu32, e.auth_id);
  data["authorizationId"] = num;
  data["authorizationName"] = auth_name;
  snprintf(num, sizeof(num), "%u", e.ts.year);
  data["timeYear"] = num;
  snprintf(num, sizeof(num), "%u", e.ts.month);
  data["timeMonth"] = num;
  snprintf(num, sizeof(num), "%u", e.ts.day);
  data["timeDay"] = num;
  snprintf(num, sizeof(num), "%u", e.ts.hour);
  data["timeHour"] = num;
  snprintf(num, sizeof(num), "%u", e.ts.minute);
  data["timeMinute"] = num;
  snprintf(num, sizeof(num), "%u", e.ts.second);
  data["timeSecond"] = num;
  // The lock's own clock, ISO-8601 local time: door/keypad events keep
  // their real time even though the host only polls the log afterwards.
  snprintf(num, sizeof(num), "%04u-%02u-%02uT%02u:%02u:%02u", e.ts.year,
           e.ts.month, e.ts.day, e.ts.hour, e.ts.minute, e.ts.second);
  data["timestamp"] = num;
  data["type"] = nuki_log_type_name(e.type);

  switch (e.type) {
  case NUKI_LOG_TYPE_LOCK_ACTION:
  case NUKI_LOG_TYPE_CALIBRATION:
  case NUKI_LOG_TYPE_INIT_RUN:
    if (e.data_len >= 4) {
      data["action"] = nuki_lock_action_name(e.data[0]);
      data["trigger"] = nuki_trigger_name(e.data[1]);
      data["completionStatus"] = nuki_completion_status_name(e.data[3]);
    }
    break;
  case NUKI_LOG_TYPE_KEYPAD_ACTION:
    if (e.data_len >= 3) {
      data["action"] = nuki_lock_action_name(e.data[0]);
      if (e.data[2] == 0x09) {
        data["trigger"] = "notAuthorized";
      } else if (e.data[2] == NUKI_COMPLETION_INVALID_CODE) {
        data["trigger"] = "invalidCode";
      } else {
        data["trigger"] = nuki_keypad_source_name(e.data[1]);
      }
      data["completionStatus"] = nuki_completion_status_name(e.data[2]);
    }
    snprintf(num, sizeof(num), "%u", nuki_log_entry_code_id(&e));
    data["codeId"] = num;
    break;
  case NUKI_LOG_TYPE_DOOR_SENSOR:
    if (e.data_len >= 1) {
      data["action"] = nuki_door_log_action_name(e.data[0]);
    }
    break;
  case NUKI_LOG_TYPE_LOGGING_ENABLED:
  case NUKI_LOG_TYPE_DOOR_SENSOR_LOGGING:
    if (e.data_len >= 1) {
      data["action"] = e.data[0] ? "Enabled" : "Disabled";
    }
    break;
  default:
    break;
  }
  ESP_LOGD(TAG, "firing %s for log #%" PRIu32, this->event_, e.index);
  this->fire_homeassistant_event(this->event_, data);
#else
  (void)e;
  (void)auth_name;
#endif
}

// ── TX ────────────────────────────────────────────────────────────────

bool NukiUartBridgeLock::send_hello_() {
  int n = nuki_uart_build_hello(this->tx_wire_.data(), this->tx_wire_.size(),
                                NUKI_UART_PROTO_V2);
  if (n < 0) {
    ESP_LOGE(TAG, "HELLO build failed (%d)", n);
    return false;
  }
  this->write_array(this->tx_wire_.data(), (size_t)n);
  this->hello_sent_ms_ = millis();
  ESP_LOGD(TAG, "-> HELLO (v1 framed, host_version=2)");
  return true;
}

uint16_t NukiUartBridgeLock::next_seq_() {
  // 0x0001..0xFFFF, never 0 (reserved for unsolicited frames)
  this->seq_++;
  if (this->seq_ == 0) {
    this->seq_ = 1;
  }
  return this->seq_;
}

bool NukiUartBridgeLock::send_cmd_(uint8_t cmd, const uint8_t *data, size_t len,
                                   uint16_t *seq_out) {
  if (this->link_ != LinkState::READY) {
    ESP_LOGD(TAG, "cmd 0x%02X not sent: link not ready", cmd);
    return false;
  }
  uint16_t seq = (this->version_ == NUKI_UART_PROTO_V2) ? this->next_seq_() : 0;
  int n;
  if (this->sec_in_use_() && this->sec_ready_()) {
    // inner [cmd][seq LE16][data] -> sealed [E0][ctr][tag][ct] -> outer v1
    uint8_t inner[3 + NUKI_UART_PAYLOAD_MAX];
    if (len > NUKI_UART_PAYLOAD_MAX || seq == 0) {
      ESP_LOGE(TAG, "cmd 0x%02X not sealable (len=%u seq=%u)", cmd,
               (unsigned)len, seq);
      return false;
    }
    inner[0] = cmd;
    inner[1] = (uint8_t)seq;
    inner[2] = (uint8_t)(seq >> 8);
    if (len > 0) {
      memcpy(inner + 3, data, len);
    }
    // Separate from sec_scratch_: a handler may send while its opened
    // inner frame is still being read from there.
    uint8_t *sealed = this->sec_seal_.data();
    int sn = nuki_seclink_seal(&this->sec_, inner, 3 + len, sealed,
                               this->sec_seal_.size());
    if (sn < 0) {
      ESP_LOGE(TAG, "seal failed for cmd 0x%02X (%d)", cmd, sn);
      return false;
    }
    n = nuki_uart_build_frame(this->tx_wire_.data(), this->tx_wire_.size(),
                              NUKI_UART_PROTO_V1, sealed[0], 0, sealed + 1,
                              (size_t)sn - 1);
  } else if (this->sec_in_use_() && this->bridge_sec_required_ &&
             cmd != NUKI_UART_CMD_PAIR_WINDOW) {
    ESP_LOGD(TAG, "cmd 0x%02X held: secure session not up (%s)", cmd,
             nuki_seclink_state_name(this->sec_.state));
    return false;
  } else if (this->sec_in_use_() && cmd != NUKI_UART_CMD_PAIR_WINDOW) {
    // bridge unpaired: nothing but the bootstrap goes out in the clear
    ESP_LOGD(TAG, "cmd 0x%02X held until the secure link is paired", cmd);
    return false;
  } else {
    n = nuki_uart_build_frame(this->tx_wire_.data(), this->tx_wire_.size(),
                              this->version_, cmd, seq, data, len);
  }
  if (n < 0) {
    ESP_LOGE(TAG, "frame build failed for cmd 0x%02X (%d)", cmd, n);
    return false;
  }
  this->write_array(this->tx_wire_.data(), (size_t)n);
  this->track_pending_(seq, cmd);
  if (seq_out != nullptr) {
    *seq_out = seq;
  }
  ESP_LOGV(TAG, "-> cmd 0x%02X seq=%u len=%u", cmd, seq, (unsigned)len);
  return true;
}

// ── RX ────────────────────────────────────────────────────────────────

void NukiUartBridgeLock::drain_rx_() {
  uint8_t chunk[64];
  size_t budget = RX_BUDGET_PER_LOOP;
  while (budget > 0) {
    size_t avail = this->available();
    if (avail == 0) {
      break;
    }
    size_t n = std::min(std::min(avail, sizeof(chunk)), budget);
    if (!this->read_array(chunk, n)) {
      break;
    }
    for (size_t i = 0; i < n; i++) {
      this->feed_rx_byte_(chunk[i]);
    }
    budget -= n;
  }
}

void NukiUartBridgeLock::feed_rx_byte_(uint8_t b) {
  if (b == 0x00) {
    if (this->rx_discard_) {
      // end of garbage / oversized frame: resync here
      this->rx_discard_ = false;
      this->rx_cobs_len_ = 0;
      return;
    }
    if (this->rx_cobs_len_ > 0) {
      int d =
          nuki_uart_cobs_decode(this->rx_frame_.data(), this->rx_frame_.size(),
                                this->rx_cobs_.data(), this->rx_cobs_len_);
      this->rx_cobs_len_ = 0;
      if (d < 0) {
        ESP_LOGW(TAG, "rx COBS error %d", d);
        return;
      }
      this->handle_frame_(this->rx_frame_.data(), (size_t)d);
    }
    // else: leading / repeated delimiter
    return;
  }
  if (this->rx_discard_) {
    return;
  }
  if (this->rx_cobs_len_ < this->rx_cobs_.size()) {
    this->rx_cobs_[this->rx_cobs_len_++] = b;
  } else {
    ESP_LOGW(TAG, "rx frame overflow, discarding to next delimiter");
    this->rx_cobs_len_ = 0;
    this->rx_discard_ = true;
  }
}

void NukiUartBridgeLock::handle_frame_(const uint8_t *frame, size_t len) {
  if (len > 0 && nuki_seclink_is_sec_frame(frame[0])) {
    this->handle_sec_frame_(frame, len);
    return;
  }
  nuki_uart_msg_t msg;
  int rc = nuki_uart_parse_frame(frame, len, this->version_, &msg);
  if (rc != NUKI_UART_OK) {
    ESP_LOGW(TAG, "rx frame rejected (%d) len=%u type=0x%02X", rc,
             (unsigned)len, frame[0]);
    return;
  }
  this->last_rx_ms_ = millis();
  ESP_LOGV(TAG, "<- 0x%02X seq=%u len=%u", msg.type, msg.seq,
           (unsigned)msg.len);

  if (msg.type == NUKI_UART_RSP_HELLO) {
    this->handle_hello_(msg);
    return;
  }
  if (this->sec_in_use_() && this->bridge_sec_required_ &&
      msg.type != NUKI_UART_RSP_ERROR) {
    // Once paired the bridge only ever speaks plaintext for HELLO and the
    // three secure-link status errors; anything else is not from it.
    ESP_LOGW(TAG, "plaintext 0x%02X while the link is secured — dropped",
             msg.type);
    return;
  }
  this->dispatch_msg_(msg);
}

// Sec-link frames: [E0..E3][...][CRC16 LE], no SEQ, no version header.
void NukiUartBridgeLock::handle_sec_frame_(const uint8_t *frame, size_t len) {
  if (len < 3) {
    return;
  }
  const size_t blen = len - 2;
  const uint16_t crc = nuki_uart_get_u16(frame + blen);
  if (crc != nuki_uart_crc16(frame, blen)) {
    ESP_LOGW(TAG, "sec frame 0x%02X CRC mismatch", frame[0]);
    return;
  }
  this->last_rx_ms_ = millis();
  if (!this->sec_in_use_()) {
    ESP_LOGD(TAG, "sec frame 0x%02X ignored (secure link %s)", frame[0],
             this->secure_link_ == SecureLinkMode::OFF ? "off" : "unavailable");
    return;
  }

  switch (frame[0]) {
  case NUKI_SECLINK_TYPE_HS_RESP: {
    if (this->sec_.state != NUKI_SECLINK_HS_SENT) {
      ESP_LOGD(TAG, "HS_RESP while %s — ignored",
               nuki_seclink_state_name(this->sec_.state));
      return;
    }
    int rc = nuki_seclink_handshake_finish(&this->sec_, frame, blen);
    if (rc == NUKI_SECLINK_OK) {
      this->on_sec_ready_();
    } else if (rc == NUKI_SECLINK_EAUTH) {
      // Wrong static key on one side: the loop retries with backoff and
      // then gives up; pair_host() is the fix.
      ESP_LOGE(TAG,
               "HS_RESP confirm tag failed (attempt %u): the bridge "
               "key does not match — re-pair with pair_host()",
               this->hs_attempts_);
      if (this->hs_attempts_ < HS_MAX_ATTEMPTS) {
        this->start_handshake_();
      }
    } else {
      ESP_LOGW(TAG, "HS_RESP rejected (%d)", rc);
    }
    return;
  }
  case NUKI_SECLINK_TYPE_DATA: {
    if (!this->sec_ready_()) {
      ESP_LOGD(TAG, "DATA frame before the session is up — dropped");
      return;
    }
    uint8_t *pt = this->sec_scratch_.data();
    int pn = nuki_seclink_open(&this->sec_, frame, blen, pt,
                               this->sec_scratch_.size());
    if (pn < 0) {
      this->sec_open_failures_++;
      ESP_LOGW(TAG, "sealed frame dropped (%s, total %" PRIu32 ")",
               pn == NUKI_SECLINK_EREPLAY ? "replay"
               : pn == NUKI_SECLINK_EAUTH ? "bad tag"
                                          : "error",
               this->sec_open_failures_);
      return;
    }
    if (pn < 3 || nuki_seclink_is_sec_frame(pt[0]) ||
        pt[0] == NUKI_UART_RSP_HELLO || pt[0] == NUKI_UART_CMD_HELLO) {
      ESP_LOGW(TAG, "sealed inner frame invalid (len=%d type=0x%02X)", pn,
               pn > 0 ? pt[0] : 0);
      return;
    }
    nuki_uart_msg_t msg;
    msg.type = pt[0];
    msg.seq = nuki_uart_get_u16(pt + 1);
    msg.data = pt + 3;
    msg.len = (size_t)pn - 3;
    ESP_LOGV(TAG, "<- sealed 0x%02X seq=%u len=%u", msg.type, msg.seq,
             (unsigned)msg.len);
    this->dispatch_msg_(msg);
    return;
  }
  case NUKI_SECLINK_TYPE_PAIR: {
    if (blen != NUKI_SECLINK_PAIR_LEN) {
      ESP_LOGW(TAG, "PAIR frame with bad length %u", (unsigned)blen);
      return;
    }
    if (!this->host_pairing_) {
      ESP_LOGW(TAG, "unsolicited PAIR from the bridge — ignored");
      return;
    }
    this->host_pairing_ = false;
    if (nuki_seclink_set_peer(&this->sec_, frame + 1) != NUKI_SECLINK_OK) {
      ESP_LOGE(TAG, "bridge sent an invalid static key");
      return;
    }
    if (!this->keys_.save_peer_pk(frame + 1)) {
      ESP_LOGE(TAG, "bridge key not persisted — pairing repeats after reboot");
    }
    this->bridge_sec_required_ = true;
    ESP_LOGI(TAG, "Secure link paired with the bridge (key stored)");
    this->hs_attempts_ = 0;
    this->start_handshake_();
    return;
  }
  default:
    ESP_LOGD(TAG, "sec frame 0x%02X from the bridge — ignored", frame[0]);
    return;
  }
}

bool NukiUartBridgeLock::send_sec_raw_(const uint8_t *body, size_t len) {
  if (len < 1) {
    return false;
  }
  int n =
      nuki_uart_build_frame(this->tx_wire_.data(), this->tx_wire_.size(),
                            NUKI_UART_PROTO_V1, body[0], 0, body + 1, len - 1);
  if (n < 0) {
    ESP_LOGE(TAG, "sec frame 0x%02X build failed (%d)", body[0], n);
    return false;
  }
  this->write_array(this->tx_wire_.data(), (size_t)n);
  return true;
}

void NukiUartBridgeLock::start_handshake_() {
  uint8_t seed[NUKI_SECLINK_KEY_LEN];
  uint8_t msg[NUKI_SECLINK_HS_INIT_LEN];
  if (!NukiSecLinkKeyStore::random_seed(seed)) {
    ESP_LOGE(TAG, "no entropy for the handshake");
    return;
  }
  int n = nuki_seclink_handshake_init(&this->sec_, seed, msg, sizeof(msg));
  volatile uint8_t *w = seed;
  for (size_t i = 0; i < sizeof(seed); i++) {
    w[i] = 0;
  }
  if (n < 0) {
    ESP_LOGE(TAG, "HS_INIT failed (%d, state %s)", n,
             nuki_seclink_state_name(this->sec_.state));
    return;
  }
  this->hs_attempts_++;
  this->hs_sent_ms_ = millis();
  if (this->send_sec_raw_(msg, (size_t)n)) {
    ESP_LOGI(TAG, "-> HS_INIT (attempt %u)", this->hs_attempts_);
  }
}

void NukiUartBridgeLock::on_sec_ready_() {
  ESP_LOGI(TAG, "Secure session established (attempt %u)", this->hs_attempts_);
  this->hs_attempts_ = 0;
  if (this->link_actions_deferred_) {
    this->link_actions_deferred_ = false;
    this->run_link_ready_actions_();
  }
}

void NukiUartBridgeLock::sec_end_session_(const char *why) {
  if (this->sec_.state == NUKI_SECLINK_READY ||
      this->sec_.state == NUKI_SECLINK_HS_SENT) {
    ESP_LOGW(TAG, "Secure session ended: %s", why);
    nuki_seclink_end_session(&this->sec_);
  }
  this->hs_attempts_ = 0;
}

void NukiUartBridgeLock::pair_host() {
  if (this->link_ != LinkState::READY) {
    ESP_LOGW(TAG, "pair_host: bridge link not ready");
    return;
  }
  if (!this->sec_available_ || this->secure_link_ == SecureLinkMode::OFF) {
    ESP_LOGW(TAG, "pair_host: secure link is off or unavailable");
    return;
  }
  if (!this->bridge_sec_supported_) {
    ESP_LOGW(TAG, "pair_host: this bridge firmware has no secure link");
    return;
  }
  uint8_t msg[NUKI_SECLINK_PAIR_LEN];
  int n = nuki_seclink_build_pair_msg(&this->sec_, msg, sizeof(msg));
  if (n < 0) {
    ESP_LOGE(TAG, "pair_host: PAIR build failed (%d)", n);
    return;
  }
  // Bootstrap: opens the window while the bridge holds no host key (else
  // 0x82 [07], harmless when a window is already open via the button).
  const uint8_t seconds = HOST_PAIR_WINDOW_S;
  this->send_cmd_(NUKI_UART_CMD_PAIR_WINDOW, &seconds, 1);
  if (this->send_sec_raw_(msg, (size_t)n)) {
    this->host_pairing_ = true;
    this->host_pair_sent_ms_ = millis();
    ESP_LOGI(TAG, "-> PAIR_WINDOW %us + PAIR (host static key)", seconds);
  }
}

void NukiUartBridgeLock::unpair_host() {
  if (!this->sec_in_use_() || !this->sec_ready_()) {
    ESP_LOGW(TAG, "unpair_host: only valid inside a secure session");
    return;
  }
  uint16_t seq = 0;
  if (this->send_cmd_(NUKI_UART_CMD_UNPAIR_HOST, nullptr, 0, &seq)) {
    ESP_LOGW(TAG, "-> UNPAIR_HOST seq=%u (bridge forgets this host)", seq);
  }
}

void NukiUartBridgeLock::dispatch_msg_(const nuki_uart_msg_t &msg) {
  if (this->link_ != LinkState::READY) {
    ESP_LOGD(TAG, "frame 0x%02X before HELLO completed, ignored", msg.type);
    return;
  }

  switch (msg.type) {
  case NUKI_UART_RSP_ACK:
    this->handle_ack_(msg);
    break;
  case NUKI_UART_RSP_ERROR:
    this->handle_error_(msg);
    break;
  case NUKI_UART_RSP_STATUS:
    this->handle_status_(msg);
    break;
  case NUKI_UART_RSP_STATE_CHANGE:
    this->handle_state_change_(msg);
    break;
  case NUKI_UART_RSP_ERROR_REPORT:
    this->handle_error_report_(msg);
    break;
  case NUKI_UART_RSP_CONN_STATUS:
    this->handle_conn_status_(msg);
    break;
  case NUKI_UART_RSP_PAIRING_COMPLETE:
    this->handle_pairing_complete_(msg);
    break;
  case NUKI_UART_RSP_DIAGNOSTICS:
    this->handle_diagnostics_(msg);
    break;
  case NUKI_UART_RSP_LOG_ENTRY:
    this->handle_log_entry_(msg);
    break;
  case NUKI_UART_RSP_KEYPAD_ENTRY:
    this->handle_keypad_entry_(msg);
    break;
  case NUKI_UART_RSP_AUTH_ENTRY:
    this->handle_auth_entry_(msg);
    break;
  default: {
    PendingRequest *p = this->find_pending_(msg.seq);
    ESP_LOGD(TAG, "<- unhandled 0x%02X seq=%u len=%u%s", msg.type, msg.seq,
             (unsigned)msg.len, p ? " (final for pending)" : "");
    if (p != nullptr) {
      this->clear_pending_(p);
    }
    break;
  }
  }
}

void NukiUartBridgeLock::handle_hello_(const nuki_uart_msg_t &msg) {
  if (msg.len < 4) {
    ESP_LOGW(TAG, "short HELLO payload (%u)", (unsigned)msg.len);
    return;
  }
  const uint8_t bridge_ver = msg.data[0];
  const uint8_t selected = msg.data[1];
  const uint16_t caps = nuki_uart_get_u16(msg.data + 2);
  ESP_LOGI(TAG, "<- HELLO bridge=v%u selected=v%u caps=0x%04X", bridge_ver,
           selected, caps);
  this->bridge_sec_supported_ = (caps & NUKI_UART_CAP_SEC_LINK_SUPPORTED) != 0;
  this->bridge_sec_required_ = (caps & NUKI_UART_CAP_SEC_REQUIRED) != 0;
  if (this->secure_link_ == SecureLinkMode::ON &&
      !this->bridge_sec_supported_) {
    ESP_LOGE(TAG, "secure_link: true but the bridge has no secure link — "
                  "commands will be refused");
  }
  if (this->secure_link_ == SecureLinkMode::OFF && this->bridge_sec_required_) {
    ESP_LOGE(TAG, "secure_link: false but the bridge holds a host key — "
                  "expect 0x82 [08] on every command");
  }

  if (selected == NUKI_UART_PROTO_V1) {
    // Boot hello (or a bridge that refused v2): the bridge is in v1.
    // Any 0x90 with selected=1 means the bridge restarted.
    this->on_bridge_restart_();
    return;
  }

  this->version_ = selected;
  this->hello_retry_ms_ = HELLO_RETRY_MIN_MS;
  if (this->link_ != LinkState::READY) {
    this->link_ = LinkState::READY;
    this->on_link_ready_();
  }
}

void NukiUartBridgeLock::handle_ack_(const nuki_uart_msg_t &msg) {
  const uint8_t for_cmd = msg.len >= 1 ? msg.data[0] : 0;
  PendingRequest *p = this->find_pending_(msg.seq);
  if (p != nullptr) {
    p->acked = true;
    // Commands whose ACK is the only reply.
    switch (for_cmd) {
    case NUKI_UART_CMD_PING:
    case NUKI_UART_CMD_SET_LINK_PROFILE:
    case NUKI_UART_CMD_SET_STATE_POLL:
    case NUKI_UART_CMD_PAIR_WINDOW:
      this->clear_pending_(p);
      break;
    case NUKI_UART_CMD_UNPAIR_HOST:
      this->clear_pending_(p);
      this->keys_.clear_peer_pk();
      nuki_seclink_end_session(&this->sec_);
      {
        nuki_seclink_static_t self_key;
        memcpy(&self_key, &this->sec_.self, sizeof(self_key));
        nuki_seclink_init(&this->sec_, NUKI_SECLINK_INITIATOR, &self_key,
                          nullptr);
        volatile uint8_t *w = reinterpret_cast<volatile uint8_t *>(&self_key);
        for (size_t i = 0; i < sizeof(self_key); i++) {
          w[i] = 0;
        }
      }
      this->bridge_sec_required_ = false;
      this->hs_attempts_ = 0;
      ESP_LOGW(TAG, "Host unpaired from the bridge: call pair_host() (opens "
                    "the window) to pair again");
      break;
    case NUKI_UART_CMD_UNPAIR:
      this->clear_pending_(p);
      this->paired_ = false;
      this->pairing_ = false;
      this->last_nuki_lock_state_ = NUKI_LOCK_STATE_UNDEFINED;
      this->publish_state(lock::LOCK_STATE_NONE);
      ESP_LOGW(TAG, "Bridge credentials cleared (UNPAIR acknowledged)");
      break;
    default:
      break;
    }
  }
  ESP_LOGV(TAG, "<- ACK cmd=0x%02X seq=%u", for_cmd, msg.seq);
}

void NukiUartBridgeLock::handle_error_(const nuki_uart_msg_t &msg) {
  const uint8_t code = msg.len >= 1 ? msg.data[0] : 0;
  PendingRequest *p = this->find_pending_(msg.seq);
  const uint8_t cmd = p ? p->cmd : 0;
  ESP_LOGW(TAG, "<- ERROR 0x%02X %s (seq=%u, cmd=0x%02X)", code,
           uart_error_name(code), msg.seq, cmd);

  if (code == NUKI_UART_ERR_NOT_PAIRED) {
    this->paired_ = false;
  }
  if (code == NUKI_UART_ERR_SECURE_REQUIRED && this->sec_in_use_()) {
    // Session lost on the bridge side (reboot missed, 120 s idle).
    this->bridge_sec_required_ = true;
    this->sec_end_session_("bridge answered SECURE_REQUIRED");
    if (this->sec_.state == NUKI_SECLINK_PAIRED) {
      this->link_actions_deferred_ = true;
      this->start_handshake_();
    } else {
      ESP_LOGE(TAG, "Bridge requires a secure session but no bridge key is "
                    "stored here: pair_host() inside its pairing window");
    }
  }
  if (code == NUKI_UART_ERR_UART_AUTH && this->sec_in_use_()) {
    ESP_LOGE(TAG, "Bridge rejected our secure frame (UART_AUTH): keys do not "
                  "match — re-pair with pair_host()");
    this->sec_end_session_("UART_AUTH");
    this->hs_attempts_ = HS_MAX_ATTEMPTS; // stop retrying by itself
  }
  if (msg.seq != 0 && msg.seq == this->action_seq_) {
    this->fail_action_(msg.seq, uart_error_name(code));
  }
  if (this->pairing_ && msg.seq == this->pair_seq_) {
    this->pairing_ = false;
    ESP_LOGE(TAG, "Pairing failed: %s", uart_error_name(code));
  }
  if (this->stream_cmd_ != 0 && msg.seq == this->stream_seq_) {
    this->end_stream_(uart_error_name(code));
  }
  if (cmd == NUKI_UART_CMD_SET_STATE_POLL &&
      (code == NUKI_UART_ERR_UNKNOWN_CMD ||
       code == NUKI_UART_ERR_UNSUPPORTED)) {
    ESP_LOGW(TAG, "Bridge firmware has no SET_STATE_POLL; door/state "
                  "freshness follows its built-in poll");
  }
  if (p != nullptr) {
    this->clear_pending_(p);
  }
}

void NukiUartBridgeLock::handle_status_(const nuki_uart_msg_t &msg) {
  PendingRequest *p = this->find_pending_(msg.seq);

  // 1-byte form: reply to UART_CMD_STATUS = paired flag
  if (msg.len == 1) {
    this->paired_ = msg.data[0] != 0;
    ESP_LOGD(TAG, "<- STATUS paired=%s", YESNO(this->paired_));
    if (p != nullptr) {
      this->clear_pending_(p);
    }
    return;
  }

  if (msg.len >= 2) {
    const uint16_t cmd_id = nuki_uart_get_u16(msg.data);
    if (cmd_id == NUKI_CMD_ID_STATUS && msg.len >= 3) {
      const uint8_t status = msg.data[2];
      const uint32_t now = millis();
      if (status == NUKI_STATUS_ACCEPTED) {
        if (msg.seq != 0 && msg.seq == this->action_seq_) {
          this->action_accepted_ms_ = now;
          ESP_LOGI(TAG,
                   "<- Status ACCEPTED for action 0x%02X after %" PRIu32 " ms",
                   this->action_cmd_, now - this->action_sent_ms_);
        } else {
          ESP_LOGD(TAG, "<- Status ACCEPTED seq=%u", msg.seq);
        }
      } else if (status == NUKI_STATUS_COMPLETE) {
        ESP_LOGD(TAG, "<- Status COMPLETE seq=%u", msg.seq);
      } else {
        ESP_LOGW(TAG, "<- Status 0x%02X seq=%u", status, msg.seq);
      }
      if (status == NUKI_STATUS_COMPLETE && this->stream_cmd_ != 0 &&
          (msg.seq == 0 || msg.seq == this->stream_seq_)) {
        this->end_stream_("Status COMPLETE");
      }
      if (p != nullptr) {
        this->clear_pending_(p); // final frame of the request
      }
      return;
    }
    if (this->handle_entry_status_(msg, cmd_id)) {
      if (p != nullptr) {
        this->clear_pending_(p);
      }
      return;
    }
    const uint8_t *body = nullptr;
    size_t blen = nuki_uart_keyturner_body(msg.data, msg.len, &body);
    if (blen >= 2) {
      this->apply_keyturner_states_(body, blen);
      if (p != nullptr) {
        this->clear_pending_(p);
      }
      return;
    }
    ESP_LOGD(TAG, "<- STATUS with lock cmd 0x%04X len=%u seq=%u", cmd_id,
             (unsigned)msg.len, msg.seq);
  }
  if (p != nullptr) {
    this->clear_pending_(p);
  }
}

// Count / id frames that travel as STATUS 0x81 with the request SEQ.
bool NukiUartBridgeLock::handle_entry_status_(const nuki_uart_msg_t &msg,
                                              uint16_t cmd_id) {
  switch (cmd_id) {
  case NUKI_CMD_ID_LOG_ENTRY_COUNT: {
    nuki_log_count_t c;
    if (nuki_uart_parse_log_count(msg.data, msg.len, &c) == NUKI_UART_OK) {
      ESP_LOGI(TAG, "<- Log Entry Count %u (logging %s, door sensor %s/%s)",
               c.count, c.logging_enabled ? "on" : "OFF",
               c.door_sensor_enabled ? "on" : "off",
               c.door_sensor_logging_enabled ? "logged" : "not logged");
    }
    return true;
  }
  case NUKI_CMD_ID_KEYPAD_CODE_COUNT: {
    uint16_t n = 0;
    if (nuki_uart_parse_keypad_count(msg.data, msg.len, &n) == NUKI_UART_OK) {
      ESP_LOGI(TAG, "<- Keypad Code Count %u", n);
    }
    return true;
  }
  case NUKI_CMD_ID_AUTH_ENTRY_COUNT: {
    uint16_t n = 0;
    if (nuki_uart_parse_auth_count(msg.data, msg.len, &n) == NUKI_UART_OK) {
      ESP_LOGD(TAG, "<- Authorization Entry Count %u", n);
    }
    return true;
  }
  case NUKI_CMD_ID_KEYPAD_CODE_ID: {
    uint16_t id = 0;
    nuki_ts_t ts;
    if (nuki_uart_parse_keypad_code_id(msg.data, msg.len, &id, &ts) ==
        NUKI_UART_OK) {
      ESP_LOGI(TAG, "<- Keypad code added: id %u (created %04u-%02u-%02u)", id,
               ts.year, ts.month, ts.day);
    }
    return true;
  }
  default:
    return false;
  }
}

void NukiUartBridgeLock::handle_log_entry_(const nuki_uart_msg_t &msg) {
  nuki_log_entry_t e;
  int rc = nuki_uart_parse_log_entry(msg.data, msg.len, &e);
  if (rc != NUKI_UART_OK) {
    ESP_LOGW(TAG, "<- LOG_ENTRY rejected (%d) len=%u", rc, (unsigned)msg.len);
    return;
  }
  if (this->stream_cmd_ == NUKI_UART_CMD_REQ_LOG_ENTRIES) {
    this->stream_entries_++;
    this->stream_last_ms_ = millis();
  }
  this->process_log_entry_(e);
}

void NukiUartBridgeLock::handle_keypad_entry_(const nuki_uart_msg_t &msg) {
  nuki_keypad_code_t k;
  int rc = nuki_uart_parse_keypad_code(msg.data, msg.len, &k);
  if (rc != NUKI_UART_OK) {
    ESP_LOGW(TAG, "<- KEYPAD_ENTRY rejected (%d) len=%u", rc,
             (unsigned)msg.len);
    return;
  }
  if (this->stream_cmd_ == NUKI_UART_CMD_REQ_KEYPAD_CODES) {
    this->stream_entries_++;
    this->stream_last_ms_ = millis();
  }
  // The code itself is deliberately not logged.
  ESP_LOGI(TAG,
           "keypad #%u '%s' %s, used %u times, created %04u-%02u-%02u, last "
           "%04u-%02u-%02u %02u:%02u%s",
           k.code_id, k.name, k.enabled ? "enabled" : "disabled", k.lock_count,
           k.created.year, k.created.month, k.created.day, k.last_active.year,
           k.last_active.month, k.last_active.day, k.last_active.hour,
           k.last_active.minute, k.time_limited ? " (time limited)" : "");
}

void NukiUartBridgeLock::handle_auth_entry_(const nuki_uart_msg_t &msg) {
  nuki_auth_entry_t a;
  int rc = nuki_uart_parse_auth_entry(msg.data, msg.len, &a);
  if (rc != NUKI_UART_OK) {
    ESP_LOGW(TAG, "<- AUTH_ENTRY rejected (%d) len=%u", rc, (unsigned)msg.len);
    return;
  }
  if (this->stream_cmd_ == NUKI_UART_CMD_REQ_AUTH_ENTRIES) {
    this->stream_entries_++;
    this->stream_last_ms_ = millis();
  }
  ESP_LOGD(TAG, "auth %" PRIu32 " type=%u '%s' %s", a.auth_id, a.id_type,
           a.name, a.enabled ? "enabled" : "disabled");
  this->remember_auth_name_(a.auth_id, a.name);
}

void NukiUartBridgeLock::handle_state_change_(const nuki_uart_msg_t &msg) {
  const uint8_t *body = nullptr;
  size_t blen = nuki_uart_keyturner_body(msg.data, msg.len, &body);
  if (blen < 2) {
    ESP_LOGW(TAG, "<- STATE_CHANGE with unexpected payload len=%u",
             (unsigned)msg.len);
    return;
  }
  this->apply_keyturner_states_(body, blen);
}

void NukiUartBridgeLock::apply_keyturner_states_(const uint8_t *body,
                                                 size_t len) {
  // Keyturner States, Nuki API v2.3.1 pp.30-35. Length-driven: only the
  // first two bytes are guaranteed, everything else is optional.
  const uint8_t nuki_state = body[0];
  const uint8_t lock_state = body[1];
  const uint8_t trigger = len > 2 ? body[2] : 0xFF;
  const int battery_pct = len > 12 ? ((body[12] >> 2) & 0x3F) * 2 : -1;
  const bool battery_critical = len > 12 && (body[12] & 0x01);
  const int last_action = len > 15 ? body[15] : -1;
  const int last_action_trigger = len > 16 ? body[16] : -1;
  const int door_sensor = len > 18 ? body[18] : -1;

  ESP_LOGD(TAG,
           "Keyturner: nuki_state=0x%02X lock_state=0x%02X trigger=0x%02X "
           "battery=%d%%%s door=%d (len=%u)",
           nuki_state, lock_state, trigger, battery_pct,
           battery_critical ? " CRITICAL" : "", door_sensor, (unsigned)len);

  // An unlatch is reported as UNLATCHING 0x07 -> UNLATCHED 0x05 -> UNLOCKED
  // 0x03, one 0x85 each a few seconds apart; the mapping folds 0x07 into
  // UNLOCKING and 0x05 into UNLOCKED and Lock::publish_state() de-dups, so
  // the entity settles once.  `changed` tracks the raw byte (for the
  // on_state_change trigger), `entity_changed` the HA-visible state.
  const lock::LockState new_state = nuki_to_esphome_state(lock_state);
  const bool changed = lock_state != this->last_nuki_lock_state_;
  const bool entity_changed = new_state != this->state;
  this->last_nuki_lock_state_ = lock_state;

  if (this->action_seq_ != 0 && changed) {
    const uint32_t now = millis();
    if (new_state == lock::LOCK_STATE_LOCKED ||
        new_state == lock::LOCK_STATE_UNLOCKED ||
        new_state == lock::LOCK_STATE_JAMMED ||
        new_state == lock::LOCK_STATE_NONE) {
      ESP_LOGI(TAG,
               "Lock state 0x%02X confirmed %" PRIu32
               " ms after action 0x%02X (accepted after %" PRIu32 " ms)",
               lock_state, now - this->action_sent_ms_, this->action_cmd_,
               this->action_accepted_ms_
                   ? this->action_accepted_ms_ - this->action_sent_ms_
                   : 0);
      this->action_seq_ = 0;
    }
  }

  if (lock_state == NUKI_LOCK_STATE_MOTOR_BLOCKED) {
    ESP_LOGE(TAG, "Nuki reports MOTOR BLOCKED");
  }

  this->publish_state(new_state);
  if (changed) {
    this->state_change_callback_.call(lock_state);
  }

  // Last Lock Action / trigger (spec p.32, bytes 15-16)
  if (last_action >= 0 && last_action != this->last_action_) {
    this->last_action_ = (int16_t)last_action;
    if (this->last_lock_action_sensor_ != nullptr) {
      this->last_lock_action_sensor_->publish_state(
          nuki_lock_action_name((uint8_t)last_action));
    }
  }
  if (last_action_trigger >= 0 &&
      last_action_trigger != this->last_action_trigger_) {
    this->last_action_trigger_ = (int16_t)last_action_trigger;
    if (this->last_lock_action_trigger_sensor_ != nullptr) {
      this->last_lock_action_trigger_sensor_->publish_state(
          nuki_trigger_name((uint8_t)last_action_trigger));
    }
  }
  if (changed && trigger == NUKI_TRIGGER_MANUAL) {
    this->publish_last_unlock_user_("Manual");
  }
  if (door_sensor >= 0) {
    this->apply_door_sensor_((uint8_t)door_sensor);
  }

  // A settled state after a change: the lock has a fresh log entry telling
  // who did it — fetch the newest entries (debounced).
  if (entity_changed && (new_state == lock::LOCK_STATE_LOCKED ||
                         new_state == lock::LOCK_STATE_UNLOCKED ||
                         new_state == lock::LOCK_STATE_JAMMED)) {
    this->schedule_log_poll_();
  }
}

void NukiUartBridgeLock::apply_door_sensor_(uint8_t state) {
  if (state == this->door_state_) {
    return;
  }
  ESP_LOGI(TAG, "Door sensor: %s (0x%02X)", nuki_door_sensor_state_name(state),
           state);
  this->door_state_ = state;
  if (this->door_sensor_state_sensor_ != nullptr) {
    this->door_sensor_state_sensor_->publish_state(
        nuki_door_sensor_state_name(state));
  }
  if (this->door_sensor_ != nullptr) {
    // Only a real reading is a state; everything else is "unavailable".
    if (state == NUKI_DOOR_OPENED) {
      this->door_sensor_->publish_state(true);
    } else if (state == NUKI_DOOR_CLOSED) {
      this->door_sensor_->publish_state(false);
    } else {
      this->door_sensor_->invalidate_state();
    }
  }
  if (this->tamper_sensor_ != nullptr) {
    if (state == NUKI_DOOR_UNAVAILABLE || state == NUKI_DOOR_DEACTIVATED) {
      this->tamper_sensor_->invalidate_state();
    } else {
      this->tamper_sensor_->publish_state(state == NUKI_DOOR_TAMPERED);
    }
  }
  this->door_state_callback_.call(state);
  if (state == NUKI_DOOR_OPENED || state == NUKI_DOOR_CLOSED ||
      state == NUKI_DOOR_TAMPERED) {
    this->schedule_log_poll_(); // door log entries carry the exact time
  }
}

void NukiUartBridgeLock::handle_error_report_(const nuki_uart_msg_t &msg) {
  // [12 00][error:1][cmd id:2 LE]  (spec p.38)
  uint8_t err = 0;
  uint16_t cmd_id = 0;
  if (msg.len >= 3) {
    err = msg.data[2];
  }
  if (msg.len >= 5) {
    cmd_id = nuki_uart_get_u16(msg.data + 3);
  }
  ESP_LOGW(TAG, "<- Lock error 0x%02X %s for Nuki cmd 0x%04X (seq=%u)", err,
           nuki_error_name(err), cmd_id, msg.seq);
  PendingRequest *p = this->find_pending_(msg.seq);
  if (msg.seq != 0 && msg.seq == this->action_seq_) {
    this->fail_action_(msg.seq, nuki_error_name(err));
  }
  if (this->stream_cmd_ != 0 && msg.seq == this->stream_seq_) {
    this->end_stream_(nuki_error_name(err));
  }
  if (err == 0x21 && p != nullptr) {
    ESP_LOGE(TAG, "K_ERROR_BAD_PIN for cmd 0x%02X: check security_pin", p->cmd);
  }
  if (p != nullptr) {
    this->clear_pending_(p);
  }
}

void NukiUartBridgeLock::handle_conn_status_(const nuki_uart_msg_t &msg) {
  if (msg.len < 2) {
    return;
  }
  const uint8_t event = msg.data[0];
  const uint8_t value = msg.data[1];
  switch (event) {
  case NUKI_UART_CONN_EVT_STATE:
    if (value != this->conn_state_) {
      ESP_LOGI(TAG, "Bridge BLE: %s -> %s", conn_state_name(this->conn_state_),
               conn_state_name(value));
    }
    this->conn_state_ = value;
    this->set_connected_(value == NUKI_UART_CONN_CONNECTED);
    break;
  case NUKI_UART_CONN_EVT_RSSI:
    this->rssi_dbm_ = (int8_t)value;
    this->rssi_valid_ = true;
    ESP_LOGD(TAG, "Bridge RSSI %d dBm", this->rssi_dbm_);
    if (this->rssi_sensor_ != nullptr) {
      this->rssi_sensor_->publish_state(this->rssi_dbm_);
    }
    break;
  case NUKI_UART_CONN_EVT_HOST_LINK:
    if (value != 0) {
      ESP_LOGW(TAG, "Bridge marked host link STALE — pinging");
      this->send_ping();
    } else {
      ESP_LOGD(TAG, "Bridge host link healthy");
    }
    break;
  case NUKI_UART_CONN_EVT_BEACON:
    ESP_LOGD(TAG, "Lock beacon state-change flag=%u", value);
    // The bridge follows the flag with its own state read; nothing to do.
    break;
  default:
    ESP_LOGD(TAG, "<- CONN_STATUS event 0x%02X value 0x%02X", event, value);
    break;
  }
}

void NukiUartBridgeLock::handle_pairing_complete_(const nuki_uart_msg_t &msg) {
  const uint32_t auth_id = msg.len >= 4 ? nuki_uart_get_u32(msg.data) : 0;
  ESP_LOGI(TAG, "<- PAIRING_COMPLETE auth_id=%" PRIu32, auth_id);
  this->paired_ = true;
  this->pairing_ = false;
  this->auth_fetched_ = false; // new authorization list
  PendingRequest *p = this->find_pending_(msg.seq);
  if (p != nullptr) {
    this->clear_pending_(p);
  }
  this->pairing_complete_callback_.call(auth_id);
  // conn_mgr restarts by itself; the state read is answered once connected.
  this->request_lock_state();
}

void NukiUartBridgeLock::handle_diagnostics_(const nuki_uart_msg_t &msg) {
  PendingRequest *p = this->find_pending_(msg.seq);
  if (p != nullptr) {
    this->clear_pending_(p);
  }
  const uint8_t *d = msg.data;
  const size_t n = msg.len;
  // v3 body is 49 bytes; v4 appends 79 (encode_diagnostics_payload() on the
  // bridge: three counters, not two — docs §12 lists armed_actions too)
  static const size_t V3_LEN = 49;
  static const size_t V4_TAIL_LEN = 4 + 4 + 4 + 1 + 1 + 2 + 2 + 1 + 3 * 20;
  if (n < 1) {
    return;
  }
  BridgeDiagnostics dg;
  dg.version = d[0];
  if (dg.version < 0x03 || n < V3_LEN) {
    ESP_LOGW(TAG, "<- DIAGNOSTICS v0x%02X len=%u: unsupported layout",
             dg.version, (unsigned)n);
    return;
  }
  size_t o = 1;
  dg.uptime_s = nuki_uart_get_u32(d + o);
  o += 4;
  dg.conn_state = d[o++];
  dg.conn_running = d[o++] != 0;
  dg.conn_ready = d[o++] != 0;
  dg.device_type = d[o++];
  dg.mtu = nuki_uart_get_u16(d + o);
  o += 2;
  dg.cmd_queue_len = d[o++];
  dg.pairing_in_progress = d[o++] != 0;
  dg.creds_valid = d[o++] != 0;
  dg.lock_addr_valid = d[o++] != 0;
  dg.scan_attempts = nuki_uart_get_u32(d + o);
  o += 4;
  dg.connect_failures = nuki_uart_get_u32(d + o);
  o += 4;
  dg.crc_failures = nuki_uart_get_u32(d + o);
  o += 4;
  dg.decrypt_failures = nuki_uart_get_u32(d + o);
  o += 4;
  dg.queue_overflows = nuki_uart_get_u32(d + o);
  o += 4;
  dg.cmd_rtt_last_ms = nuki_uart_get_u32(d + o);
  o += 4;
  dg.cmd_rtt_avg_ms = nuki_uart_get_u32(d + o);
  o += 4;
  dg.last_connect_fail_reason = d[o++];
  dg.bridge_proto_version = d[o++];
  dg.selected_proto_version = d[o++];
  dg.host_link_stale = d[o++] != 0;
  dg.host_idle_s = nuki_uart_get_u16(d + o);
  o += 2;

  if (dg.version >= 0x04 && n >= o + V4_TAIL_LEN) {
    dg.has_v4 = true;
    dg.replay_rejected = nuki_uart_get_u32(d + o);
    o += 4;
    dg.armed_fallbacks = nuki_uart_get_u32(d + o);
    o += 4;
    dg.armed_actions = nuki_uart_get_u32(d + o);
    o += 4;
    dg.arm_state = d[o++];
    dg.link_profile = d[o++];
    dg.conn_interval_1250us = nuki_uart_get_u16(d + o);
    o += 2;
    dg.dle_tx_max_len = nuki_uart_get_u16(d + o);
    o += 2;
    dg.single_pdu_ok = d[o++] != 0;
    for (auto &st : dg.stage) {
      st.count = nuki_uart_get_u32(d + o);
      st.last_us = nuki_uart_get_u32(d + o + 4);
      st.min_us = nuki_uart_get_u32(d + o + 8);
      st.max_us = nuki_uart_get_u32(d + o + 12);
      st.avg_us = nuki_uart_get_u32(d + o + 16);
      o += 20;
    }
  } else if (dg.version >= 0x04) {
    ESP_LOGW(TAG, "<- DIAGNOSTICS v0x%02X but only %u bytes: v4 tail ignored",
             dg.version, (unsigned)n);
  }
  // v5 appends 6 bytes (secure link + state poll); a log-tail cursor may
  // follow in a later bridge build — both parsed only when present.
  if (dg.version >= 0x05 && dg.has_v4 && n >= o + 6) {
    dg.has_v5 = true;
    dg.sec_state = d[o++];
    dg.pair_window_open = d[o++] != 0;
    dg.pair_window_remaining_s = nuki_uart_get_u16(d + o);
    o += 2;
    dg.state_poll_interval_s = nuki_uart_get_u16(d + o);
    o += 2;
    if (n >= o + 4) {
      dg.has_logtail = true;
      dg.logtail_last_index = nuki_uart_get_u32(d + o);
      o += 4;
    }
  }
  if (o < n) {
    ESP_LOGD(TAG, "<- DIAGNOSTICS: %u trailing bytes not parsed",
             (unsigned)(n - o));
  }
  dg.valid = true;
  this->diag_ = dg;

  // Keep the lock/pairing view in sync with the bridge.
  this->paired_ = dg.creds_valid;
  this->conn_state_ = dg.conn_state;
  this->set_connected_(dg.conn_state == NUKI_UART_CONN_CONNECTED);

  ESP_LOGI(TAG,
           "Bridge diag v%u: up=%" PRIu32 "s ble=%s ready=%u dev=%s "
           "mtu=%u paired=%u pairing=%u q=%u rtt=%" PRIu32 "/%" PRIu32
           "ms proto=%u/%u stale=%u idle=%us",
           dg.version, dg.uptime_s, conn_state_name(dg.conn_state),
           dg.conn_ready, device_type_name(dg.device_type), dg.mtu,
           dg.creds_valid, dg.pairing_in_progress, dg.cmd_queue_len,
           dg.cmd_rtt_last_ms, dg.cmd_rtt_avg_ms, dg.bridge_proto_version,
           dg.selected_proto_version, dg.host_link_stale, dg.host_idle_s);
  ESP_LOGI(TAG,
           "  counters: scan=%" PRIu32 " connfail=%" PRIu32 " crc=%" PRIu32
           " decrypt=%" PRIu32 " qovf=%" PRIu32 " lastfail=0x%02X",
           dg.scan_attempts, dg.connect_failures, dg.crc_failures,
           dg.decrypt_failures, dg.queue_overflows,
           dg.last_connect_fail_reason);
  if (dg.has_v5) {
    ESP_LOGI(TAG,
             "  secure: sec_state=%u window=%s (%us left) state_poll=%us%s",
             dg.sec_state, dg.pair_window_open ? "open" : "closed",
             dg.pair_window_remaining_s, dg.state_poll_interval_s,
             this->sec_ready_() ? " [session up]" : "");
    if (dg.has_logtail) {
      ESP_LOGI(TAG, "  logtail: last index %" PRIu32, dg.logtail_last_index);
    }
  }
  if (dg.has_v4) {
    ESP_LOGI(TAG,
             "  armed: arm_state=%u profile=%s ci=%u x1.25ms dle=%u "
             "single_pdu=%u replay_rej=%" PRIu32 " fallbacks=%" PRIu32
             " armed_actions=%" PRIu32,
             dg.arm_state, dg.link_profile ? "armed" : "eco",
             dg.conn_interval_1250us, dg.dle_tx_max_len, dg.single_pdu_ok,
             dg.replay_rejected, dg.armed_fallbacks, dg.armed_actions);
    static const char *const STAGE_NAMES[3] = {
        "uart_rx->gatt_write", "gatt_write->accepted", "accepted->confirmed"};
    for (size_t i = 0; i < dg.stage.size(); i++) {
      const auto &st = dg.stage[i];
      ESP_LOGI(TAG,
               "  stage%u %-22s n=%" PRIu32 " last=%" PRIu32 "us min=%" PRIu32
               "us max=%" PRIu32 "us avg=%" PRIu32 "us",
               (unsigned)i, STAGE_NAMES[i], st.count, st.last_us, st.min_us,
               st.max_us, st.avg_us);
    }
  }

  if (this->auto_pair_ && !dg.creds_valid && !dg.pairing_in_progress &&
      !this->pairing_ && !this->pair_attempted_) {
    ESP_LOGI(TAG, "Bridge is unpaired and auto_pair is on — sending PAIR");
    this->pair();
  }
  this->publish_diagnostics_text_();
  this->maybe_request_auth_entries_();
}

void NukiUartBridgeLock::publish_diagnostics_text_() {
  if (this->diagnostics_sensor_ == nullptr || !this->diag_.valid) {
    return;
  }
  const BridgeDiagnostics &dg = this->diag_;
  char buf[240];
  int n = snprintf(buf, sizeof(buf),
                   "v%u up=%" PRIu32 "s ble=%s dev=%s mtu=%u paired=%u "
                   "rssi=%d rtt=%" PRIu32 "ms proto=%u/%u stale=%u crc=%" PRIu32
                   " dec=%" PRIu32,
                   dg.version, dg.uptime_s, conn_state_name(dg.conn_state),
                   device_type_name(dg.device_type), dg.mtu, dg.creds_valid,
                   this->rssi_valid_ ? this->rssi_dbm_ : 0, dg.cmd_rtt_avg_ms,
                   dg.bridge_proto_version, dg.selected_proto_version,
                   dg.host_link_stale, dg.crc_failures, dg.decrypt_failures);
  if (dg.has_v4 && n > 0 && (size_t)n < sizeof(buf)) {
    snprintf(buf + n, sizeof(buf) - (size_t)n,
             " arm=%u prof=%s ci=%u pdu=%u s0=%" PRIu32 "us s1=%" PRIu32
             "us s2=%" PRIu32 "us",
             dg.arm_state, dg.link_profile ? "armed" : "eco",
             dg.conn_interval_1250us, dg.single_pdu_ok, dg.stage[0].avg_us,
             dg.stage[1].avg_us, dg.stage[2].avg_us);
    n = (int)strlen(buf);
  }
  if (dg.has_v5 && n > 0 && (size_t)n < sizeof(buf)) {
    snprintf(buf + n, sizeof(buf) - (size_t)n, " sec=%u poll=%us", dg.sec_state,
             dg.state_poll_interval_s);
  }
  this->diagnostics_sensor_->publish_state(buf);
}

// ── Link bookkeeping ──────────────────────────────────────────────────

void NukiUartBridgeLock::on_link_ready_() {
  ESP_LOGI(TAG, "Bridge link ready (framing v%u)", this->version_);
  this->clear_all_pending_();
  this->last_ping_ms_ = millis();
  this->last_rx_ms_ = this->last_ping_ms_;
  this->pair_attempted_ = false;
  if (!this->sec_in_use_()) {
    this->run_link_ready_actions_();
    return;
  }
  // Everything after HELLO travels sealed: hold the usual requests until
  // the handshake is done (or the bridge has been paired first).
  this->link_actions_deferred_ = true;
  this->hs_attempts_ = 0;
  if (this->sec_.state == NUKI_SECLINK_UNPAIRED) {
    if (!this->bridge_sec_required_) {
      ESP_LOGI(TAG, "Secure link: bridge unpaired — bootstrapping (TOFU)");
      this->pair_host();
    } else {
      ESP_LOGE(TAG, "Secure link: the bridge holds another host's key. Open "
                    "its pairing window (button / reboot after UNPAIR_HOST) "
                    "and call pair_host()");
    }
    return;
  }
  if (!this->bridge_sec_required_) {
    // We remember a bridge key but the bridge remembers no host: it was
    // re-flashed or unpaired.  Its window is open on such a boot.
    ESP_LOGW(TAG, "Secure link: bridge reports no host key — re-pairing");
    this->pair_host();
    return;
  }
  this->start_handshake_();
}

void NukiUartBridgeLock::run_link_ready_actions_() {
  this->set_runtime_link_profile(this->link_profile_);
  this->set_runtime_state_poll(this->state_poll_s_);
  this->request_diagnostics(); // learns creds_valid / device_type / arm state
  this->request_lock_state();
}

void NukiUartBridgeLock::on_bridge_restart_() {
  if (this->link_ == LinkState::READY) {
    ESP_LOGW(TAG, "Bridge restarted (boot HELLO seen) — renegotiating");
  } else {
    ESP_LOGD(TAG, "Bridge in v1 framing — sending HELLO");
  }
  this->link_ = LinkState::HELLO_PENDING;
  this->version_ = NUKI_UART_PROTO_V1;
  this->hello_retry_ms_ = HELLO_RETRY_MIN_MS;
  this->clear_all_pending_();
  this->pairing_ = false;
  this->action_seq_ = 0;
  this->end_stream_("bridge restart");
  this->sec_end_session_("bridge restarted");
  this->host_pairing_ = false;
  this->set_connected_(false);
  // Renegotiate now, unless a HELLO just went out (a bridge that keeps
  // answering selected=1 must not turn this into a HELLO storm); the loop
  // retries with backoff otherwise.
  if (millis() - this->hello_sent_ms_ >= HELLO_RETRY_MIN_MS) {
    this->send_hello_();
  }
}

void NukiUartBridgeLock::on_link_lost_() {
  ESP_LOGE(TAG, "No frame from the bridge for %" PRIu32 " ms — link lost",
           LINK_LOST_MS);
  this->link_ = LinkState::HELLO_PENDING;
  this->version_ = NUKI_UART_PROTO_V1;
  this->hello_retry_ms_ = HELLO_RETRY_MIN_MS;
  this->clear_all_pending_();
  this->pairing_ = false;
  this->action_seq_ = 0;
  this->end_stream_("link lost");
  this->sec_end_session_("link lost");
  this->host_pairing_ = false;
  this->last_rx_ms_ = millis();
  this->set_connected_(false);
  this->publish_state(lock::LOCK_STATE_NONE);
  this->send_hello_();
}

void NukiUartBridgeLock::set_connected_(bool connected) {
  if (connected == this->connected_) {
    return;
  }
  this->connected_ = connected;
  if (this->connected_sensor_ != nullptr) {
    this->connected_sensor_->publish_state(connected);
  }
  if (connected) {
    this->request_lock_state();
    this->maybe_request_auth_entries_();
  } else {
    // The lock is unreachable: report unknown rather than a stale state.
    this->publish_state(lock::LOCK_STATE_NONE);
  }
}

void NukiUartBridgeLock::track_pending_(uint16_t seq, uint8_t cmd) {
  if (seq == 0) {
    return; // v1 framing has no correlation
  }
  PendingRequest *slot = nullptr;
  uint32_t oldest = UINT32_MAX;
  for (auto &p : this->pending_) {
    if (!p.used) {
      slot = &p;
      break;
    }
    if (p.sent_ms < oldest) {
      oldest = p.sent_ms;
      slot = &p;
    }
  }
  if (slot->used) {
    ESP_LOGD(TAG, "pending table full, dropping seq=%u cmd=0x%02X", slot->seq,
             slot->cmd);
  }
  slot->used = true;
  slot->acked = false;
  slot->seq = seq;
  slot->cmd = cmd;
  slot->sent_ms = millis();
}

PendingRequest *NukiUartBridgeLock::find_pending_(uint16_t seq) {
  if (seq == 0) {
    return nullptr;
  }
  for (auto &p : this->pending_) {
    if (p.used && p.seq == seq) {
      return &p;
    }
  }
  return nullptr;
}

void NukiUartBridgeLock::clear_pending_(PendingRequest *p) {
  if (p != nullptr) {
    p->used = false;
  }
}

void NukiUartBridgeLock::clear_all_pending_() {
  for (auto &p : this->pending_) {
    p.used = false;
  }
}

void NukiUartBridgeLock::expire_pending_(uint32_t now) {
  for (auto &p : this->pending_) {
    if (!p.used || now - p.sent_ms < REQUEST_TIMEOUT_MS) {
      continue;
    }
    ESP_LOGW(TAG, "Request seq=%u cmd=0x%02X timed out (%s)", p.seq, p.cmd,
             p.acked ? "acked, no final response" : "never acked");
    if (p.seq == this->action_seq_) {
      this->fail_action_(p.seq, "timeout");
    }
    p.used = false;
  }
}

void NukiUartBridgeLock::fail_action_(uint16_t seq, const char *why) {
  ESP_LOGW(TAG, "Action 0x%02X seq=%u failed: %s", this->action_cmd_, seq, why);
  this->action_seq_ = 0;
  this->publish_state(lock::LOCK_STATE_NONE);
  if (this->connected_) {
    this->request_lock_state(); // resync from the lock
  }
}

} // namespace nuki_uart_bridge
} // namespace esphome
