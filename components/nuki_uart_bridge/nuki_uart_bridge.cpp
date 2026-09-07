#include "nuki_uart_bridge.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>

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
  this->send_hello_();
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
  int n = nuki_uart_build_frame(this->tx_wire_.data(), this->tx_wire_.size(),
                                this->version_, cmd, seq, data, len);
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
      this->clear_pending_(p);
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
  if (msg.seq != 0 && msg.seq == this->action_seq_) {
    this->fail_action_(msg.seq, uart_error_name(code));
  }
  if (this->pairing_ && msg.seq == this->pair_seq_) {
    this->pairing_ = false;
    ESP_LOGE(TAG, "Pairing failed: %s", uart_error_name(code));
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
      if (p != nullptr) {
        this->clear_pending_(p); // final frame of the request
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
  const int door_sensor = len > 18 ? body[18] : -1;

  ESP_LOGD(TAG,
           "Keyturner: nuki_state=0x%02X lock_state=0x%02X trigger=0x%02X "
           "battery=%d%%%s door=%d (len=%u)",
           nuki_state, lock_state, trigger, battery_pct,
           battery_critical ? " CRITICAL" : "", door_sensor, (unsigned)len);

  const lock::LockState new_state = nuki_to_esphome_state(lock_state);
  const bool changed = lock_state != this->last_nuki_lock_state_;
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
  // v3 body is 49 bytes; v4 appends 75 (docs/host-integration.md §12)
  static const size_t V3_LEN = 49;
  static const size_t V4_TAIL_LEN = 4 + 4 + 1 + 1 + 2 + 2 + 1 + 3 * 20;
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
  if (dg.has_v4) {
    ESP_LOGI(TAG,
             "  armed: arm_state=%u profile=%s ci=%u x1.25ms dle=%u "
             "single_pdu=%u replay_rej=%" PRIu32 " fallbacks=%" PRIu32,
             dg.arm_state, dg.link_profile ? "armed" : "eco",
             dg.conn_interval_1250us, dg.dle_tx_max_len, dg.single_pdu_ok,
             dg.replay_rejected, dg.armed_fallbacks);
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
}

void NukiUartBridgeLock::publish_diagnostics_text_() {
  if (this->diagnostics_sensor_ == nullptr || !this->diag_.valid) {
    return;
  }
  const BridgeDiagnostics &dg = this->diag_;
  char buf[224];
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
  this->set_runtime_link_profile(this->link_profile_);
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
