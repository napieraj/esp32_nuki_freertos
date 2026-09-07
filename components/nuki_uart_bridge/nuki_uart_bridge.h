#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/lock/lock.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include <array>
#include <cstdint>

#include "nuki_uart_framing.h"

namespace esphome {
namespace nuki_uart_bridge {

static const char *const TAG = "nuki_uart_bridge.lock";

/* Host protocol timing (docs/host-integration.md on the bridge side). */
static const uint32_t HELLO_RETRY_MIN_MS = 1000;
static const uint32_t HELLO_RETRY_MAX_MS = 8000;
static const uint32_t PING_INTERVAL_MS = 30000;   // bridge stale after 120 s
static const uint32_t LINK_LOST_MS = 90000;       // 3 missed pings
static const uint32_t REQUEST_TIMEOUT_MS = 20000; // bridge worst case ~16 s
static const uint32_t PAIR_TIMEOUT_MS = 90000;
static const size_t RX_BUDGET_PER_LOOP = 512;
static const size_t PENDING_SLOTS = 8;

enum class LinkState : uint8_t { HELLO_PENDING, READY };

struct PendingRequest {
  uint16_t seq{0};
  uint8_t cmd{0};
  uint32_t sent_ms{0};
  bool acked{false};
  bool used{false};
};

/* Parsed bridge diagnostics (0x8C), v0x03 body + optional v0x04 tail. */
struct BridgeDiagnostics {
  bool valid{false};
  uint8_t version{0};
  uint32_t uptime_s{0};
  uint8_t conn_state{0};
  bool conn_running{false};
  bool conn_ready{false};
  uint8_t device_type{0};
  uint16_t mtu{0};
  uint8_t cmd_queue_len{0};
  bool pairing_in_progress{false};
  bool creds_valid{false};
  bool lock_addr_valid{false};
  uint32_t scan_attempts{0};
  uint32_t connect_failures{0};
  uint32_t crc_failures{0};
  uint32_t decrypt_failures{0};
  uint32_t queue_overflows{0};
  uint32_t cmd_rtt_last_ms{0};
  uint32_t cmd_rtt_avg_ms{0};
  uint8_t last_connect_fail_reason{0};
  uint8_t bridge_proto_version{0};
  uint8_t selected_proto_version{0};
  bool host_link_stale{false};
  uint16_t host_idle_s{0};
  // v0x04
  bool has_v4{false};
  uint32_t replay_rejected{0};
  uint32_t armed_fallbacks{0};
  uint8_t arm_state{0};
  uint8_t link_profile{0};
  uint16_t conn_interval_1250us{0};
  uint16_t dle_tx_max_len{0};
  bool single_pdu_ok{false};
  struct Stage {
    uint32_t count{0};
    uint32_t last_us{0};
    uint32_t min_us{0};
    uint32_t max_us{0};
    uint32_t avg_us{0};
  };
  std::array<Stage, 3> stage{};
};

class NukiUartBridgeLock : public lock::Lock,
                           public Component,
                           public uart::UARTDevice {
public:
  NukiUartBridgeLock() { this->traits.set_supports_open(true); }

  void set_pin(uint32_t pin) { this->pin_ = pin; }
  void set_device_type(uint8_t t) { this->device_type_ = t; }
  void set_pair_as(uint8_t id_type) { this->id_type_ = id_type; }
  void set_link_profile(uint8_t p) { this->link_profile_ = p; }
  void set_app_id(uint32_t id) { this->app_id_ = id; }
  void set_poll_interval(uint32_t ms) { this->poll_interval_ms_ = ms; }
  void set_auto_pair(bool v) { this->auto_pair_ = v; }
  void set_connected_binary_sensor(binary_sensor::BinarySensor *s) {
    this->connected_sensor_ = s;
  }
  void set_rssi_sensor(sensor::Sensor *s) { this->rssi_sensor_ = s; }
  void set_diagnostics_text_sensor(text_sensor::TextSensor *s) {
    this->diagnostics_sensor_ = s;
  }

  void add_on_pairing_complete_callback(std::function<void(uint32_t)> &&cb) {
    this->pairing_complete_callback_.add(std::move(cb));
  }
  void add_on_state_change_callback(std::function<void(uint8_t)> &&cb) {
    this->state_change_callback_.add(std::move(cb));
  }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  /* Operator actions (lambda-callable). */
  void pair();
  void unpair();
  void request_diagnostics();
  void request_lock_state();
  void send_ping();
  void set_runtime_link_profile(uint8_t profile);

  bool is_paired() const { return this->paired_; }
  bool is_connected() const { return this->connected_; }
  bool is_link_ready() const { return this->link_ == LinkState::READY; }
  const BridgeDiagnostics &get_diagnostics() const { return this->diag_; }

protected:
  void control(const lock::LockCall &call) override;
  void open_latch() override;

  // ── TX ──
  bool send_hello_();
  uint16_t next_seq_();
  bool send_cmd_(uint8_t cmd, const uint8_t *data, size_t len,
                 uint16_t *seq_out = nullptr);
  bool send_action_(uint8_t cmd, lock::LockState optimistic);

  // ── RX ──
  void drain_rx_();
  void feed_rx_byte_(uint8_t b);
  void handle_frame_(const uint8_t *frame, size_t len);
  void handle_hello_(const nuki_uart_msg_t &msg);
  void handle_ack_(const nuki_uart_msg_t &msg);
  void handle_error_(const nuki_uart_msg_t &msg);
  void handle_status_(const nuki_uart_msg_t &msg);
  void handle_state_change_(const nuki_uart_msg_t &msg);
  void handle_error_report_(const nuki_uart_msg_t &msg);
  void handle_conn_status_(const nuki_uart_msg_t &msg);
  void handle_pairing_complete_(const nuki_uart_msg_t &msg);
  void handle_diagnostics_(const nuki_uart_msg_t &msg);
  void apply_keyturner_states_(const uint8_t *body, size_t len);

  // ── bookkeeping ──
  void on_link_ready_();
  void on_bridge_restart_();
  void on_link_lost_();
  void set_connected_(bool connected);
  void track_pending_(uint16_t seq, uint8_t cmd);
  PendingRequest *find_pending_(uint16_t seq);
  void clear_pending_(PendingRequest *p);
  void clear_all_pending_();
  void expire_pending_(uint32_t now);
  void fail_action_(uint16_t seq, const char *why);
  void publish_diagnostics_text_();

  static lock::LockState nuki_to_esphome_state(uint8_t nuki_lock_state);
  static const char *uart_error_name(uint8_t code);
  static const char *nuki_error_name(uint8_t code);
  static const char *conn_state_name(uint8_t state);
  static const char *device_type_name(uint8_t t);

  // config
  uint32_t pin_{0};
  uint8_t device_type_{NUKI_UART_DEVICE_AUTO};
  uint8_t id_type_{NUKI_UART_ID_TYPE_APP};
  uint8_t link_profile_{NUKI_UART_LINK_PROFILE_ARMED};
  uint32_t app_id_{0};
  uint32_t poll_interval_ms_{60000};
  bool auto_pair_{true};

  binary_sensor::BinarySensor *connected_sensor_{nullptr};
  sensor::Sensor *rssi_sensor_{nullptr};
  text_sensor::TextSensor *diagnostics_sensor_{nullptr};
  CallbackManager<void(uint32_t)> pairing_complete_callback_;
  CallbackManager<void(uint8_t)> state_change_callback_;

  // link
  LinkState link_{LinkState::HELLO_PENDING};
  uint8_t version_{NUKI_UART_PROTO_V1};
  uint16_t seq_{0};
  uint32_t hello_sent_ms_{0};
  uint32_t hello_retry_ms_{HELLO_RETRY_MIN_MS};
  uint32_t last_rx_ms_{0};
  uint32_t last_ping_ms_{0};
  uint32_t last_poll_ms_{0};
  uint32_t link_lost_at_ms_{0};

  // RX assembly (COBS bytes between 0x00 delimiters)
  std::array<uint8_t, NUKI_UART_RX_WIRE_MAX> rx_cobs_{};
  size_t rx_cobs_len_{0};
  bool rx_discard_{true}; // discard until the first delimiter
  std::array<uint8_t, NUKI_UART_RX_FRAME_MAX> rx_frame_{};
  std::array<uint8_t, NUKI_UART_WIRE_MAX> tx_wire_{};

  // requests
  std::array<PendingRequest, PENDING_SLOTS> pending_{};
  uint16_t action_seq_{0};
  uint32_t action_sent_ms_{0};
  uint32_t action_accepted_ms_{0};
  uint8_t action_cmd_{0};
  uint16_t pair_seq_{0};
  uint32_t pair_sent_ms_{0};
  bool pair_attempted_{false};

  // lock / bridge state
  bool paired_{false};
  bool pairing_{false};
  bool connected_{false};
  uint8_t conn_state_{NUKI_UART_CONN_IDLE};
  uint8_t last_nuki_lock_state_{NUKI_LOCK_STATE_UNDEFINED};
  int8_t rssi_dbm_{0};
  bool rssi_valid_{false};
  BridgeDiagnostics diag_{};
};

} // namespace nuki_uart_bridge
} // namespace esphome
