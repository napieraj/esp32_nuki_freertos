#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/components/lock/lock.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#ifdef USE_API
#include "esphome/components/api/custom_api_device.h"
#endif
#ifdef USE_TIME
#include "esphome/components/time/real_time_clock.h"
#endif

#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include "nuki_uart_entries.h"
#include "nuki_uart_framing.h"
#include "nuki_uart_seclink.h"

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

/* Entry streams (0x87 / 0x88 / 0x89) and the event-log pipeline. */
static const uint32_t STREAM_IDLE_MS = 8000;    // no entry for 8 s = done
static const uint32_t LOG_POLL_DELAY_MS = 2000; // after a settled state
static const uint32_t AUTH_REFRESH_MS = 6 * 3600 * 1000UL;
static const uint16_t LOG_REQUEST_MAX = 50;
static const uint16_t AUTH_REQUEST_COUNT = 32;
static const uint16_t KEYPAD_REQUEST_COUNT = 0xFFFF;
static const size_t AUTH_NAME_SLOTS = 16;

/* Secure link (bridge docs/host-integration.md §15). */
static const uint32_t HS_RETRY_MS = 2000; // bridge drops HS_INIT < 250 ms
static const uint8_t HS_MAX_ATTEMPTS = 3;
static const uint32_t HOST_PAIR_TIMEOUT_MS = 5000;
static const uint8_t HOST_PAIR_WINDOW_S = 120;
static const size_t SEC_SCRATCH_LEN = NUKI_UART_RX_FRAME_MAX;

/* Action robustness (survey S3 D / S4 F7-F8). */
static const uint32_t TRANSITION_WATCHDOG_MS =
    5000; // ACCEPTED, no settled 0x85
static const uint32_t ACTION_RETRY_WINDOW_MS =
    10000; // one retry after CONNECTED
static const uint32_t ACTION_IGNORE_WINDOW_MS =
    6000;                                   // after API / link (re)connect
static const uint32_t PAIR_RETRY_MS = 5000; // pairing_mode switch on
static const uint32_t TIME_UPDATE_INTERVAL_MS = 24UL * 3600UL * 1000UL;
static const uint32_t TIME_SYNC_DELAY_MS = 30000;
static const uint16_t TIME_UPDATE_MIN_YEAR = 2025; // nuki_hub NTP sanity guard

enum class SecureLinkMode : uint8_t { OFF = 0, AUTO = 1, ON = 2 };

enum class LinkState : uint8_t { HELLO_PENDING, READY };

/* Security PIN lifecycle (same states and names as the BLE component). */
enum class PinState : uint8_t { NOT_SET = 0, SET = 1, VALID = 2, INVALID = 3 };

/* allowed_actions bitmask, one bit per action UART command. */
enum ActionBit : uint16_t {
  ACTION_BIT_UNLOCK = 1 << 0,
  ACTION_BIT_LOCK = 1 << 1,
  ACTION_BIT_UNLATCH = 1 << 2,
  ACTION_BIT_LOCK_N_GO = 1 << 3,
  ACTION_BIT_LOCK_N_GO_UNLATCH = 1 << 4,
  ACTION_BIT_FULL_LOCK = 1 << 5,
  ACTION_BIT_FOB_1 = 1 << 6,
  ACTION_BIT_FOB_2 = 1 << 7,
  ACTION_BIT_FOB_3 = 1 << 8,
  ACTION_BIT_ALL = 0x01FF,
};

/* Persisted PIN override (ESPHome preferences, keyed by the lock entity). */
struct PinRecord {
  uint32_t magic;
  uint32_t pin;
  uint8_t state;
};

struct PendingRequest {
  uint16_t seq{0};
  uint8_t cmd{0};
  uint32_t sent_ms{0};
  bool acked{false};
  bool used{false};
};

/* auth_id -> name, learned from the 0x87 Authorization Entry stream. */
struct AuthName {
  uint32_t auth_id{0};
  char name[NUKI_AUTH_NAME_LEN + 1]{};
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
  // v0x05
  bool has_v5{false};
  uint8_t sec_state{0};
  bool pair_window_open{false};
  uint16_t pair_window_remaining_s{0};
  uint16_t state_poll_interval_s{0};
  bool has_logtail{false};
  uint32_t logtail_last_index{0};
  bool has_pin_flags{false};
  uint8_t pin_flags{0};
  // v0x04
  bool has_v4{false};
  uint32_t replay_rejected{0};
  uint32_t armed_fallbacks{0};
  uint32_t armed_actions{0};
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
                           public uart::UARTDevice
#ifdef USE_API
    ,
                           public api::CustomAPIDevice
#endif
{
public:
  NukiUartBridgeLock() { this->traits.set_supports_open(true); }

  void set_pin(uint32_t pin) { this->pin_config_ = pin; }
  void set_pairing_mode_timeout(uint32_t s) {
    this->pairing_mode_timeout_s_ = s;
  }
  void set_allowed_actions(uint16_t mask) { this->allowed_actions_ = mask; }
  /// Lock Action name suffix (<= 20 bytes) the bridge appends to every
  /// action; empty = the ESPHome friendly name.
  void set_action_suffix(const char *suffix) { this->action_suffix_ = suffix; }
#ifdef USE_TIME
  void set_time(time::RealTimeClock *t) { this->time_ = t; }
#endif
  void set_state_poll_interval(uint16_t s) { this->state_poll_s_ = s; }
  void set_secure_link(SecureLinkMode m) { this->secure_link_ = m; }
  void set_event_log_count(uint8_t n) { this->event_log_count_ = n; }
  /// Home Assistant event name ("esphome.<name>"); "esphome.none" disables.
  void set_event(const char *event) {
    this->event_ = event;
    this->send_events_ = strcmp(event, "esphome.none") != 0;
  }
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
  void set_last_unlock_user_text_sensor(text_sensor::TextSensor *s) {
    this->last_unlock_user_sensor_ = s;
  }
  void set_last_lock_action_text_sensor(text_sensor::TextSensor *s) {
    this->last_lock_action_sensor_ = s;
  }
  void set_last_lock_action_trigger_text_sensor(text_sensor::TextSensor *s) {
    this->last_lock_action_trigger_sensor_ = s;
  }
  void set_door_sensor_state_text_sensor(text_sensor::TextSensor *s) {
    this->door_sensor_state_sensor_ = s;
  }
  void set_door_sensor_binary_sensor(binary_sensor::BinarySensor *s) {
    this->door_sensor_ = s;
  }
  void set_tamper_binary_sensor(binary_sensor::BinarySensor *s) {
    this->tamper_sensor_ = s;
  }
  void set_paired_binary_sensor(binary_sensor::BinarySensor *s) {
    this->paired_sensor_ = s;
  }
  void set_battery_level_sensor(sensor::Sensor *s) {
    this->battery_level_sensor_ = s;
  }
  void set_battery_critical_binary_sensor(binary_sensor::BinarySensor *s) {
    this->battery_critical_sensor_ = s;
  }
  void set_battery_charging_binary_sensor(binary_sensor::BinarySensor *s) {
    this->battery_charging_sensor_ = s;
  }
  void
  set_keypad_battery_critical_binary_sensor(binary_sensor::BinarySensor *s) {
    this->keypad_battery_critical_sensor_ = s;
  }
  void set_door_sensor_battery_critical_binary_sensor(
      binary_sensor::BinarySensor *s) {
    this->door_sensor_battery_critical_sensor_ = s;
  }
  void set_night_mode_binary_sensor(binary_sensor::BinarySensor *s) {
    this->night_mode_sensor_ = s;
  }
  void set_keypad_paired_binary_sensor(binary_sensor::BinarySensor *s) {
    this->keypad_paired_sensor_ = s;
  }
  void set_pin_status_text_sensor(text_sensor::TextSensor *s) {
    this->pin_status_sensor_ = s;
  }
  void set_nuki_state_text_sensor(text_sensor::TextSensor *s) {
    this->nuki_state_sensor_ = s;
  }
  void set_last_lock_action_completion_status_text_sensor(
      text_sensor::TextSensor *s) {
    this->completion_status_sensor_ = s;
  }
  void set_door_security_state_text_sensor(text_sensor::TextSensor *s) {
    this->door_security_state_sensor_ = s;
  }
  void set_firmware_version_text_sensor(text_sensor::TextSensor *s) {
    this->firmware_version_sensor_ = s;
  }
  void set_hardware_version_text_sensor(text_sensor::TextSensor *s) {
    this->hardware_version_sensor_ = s;
  }
  void set_lock_name_text_sensor(text_sensor::TextSensor *s) {
    this->lock_name_sensor_ = s;
  }
  void set_nuki_id_text_sensor(text_sensor::TextSensor *s) {
    this->nuki_id_sensor_ = s;
  }
  void set_pairing_mode_switch(switch_::Switch *s) {
    this->pairing_mode_switch_ = s;
  }

  void add_on_pairing_complete_callback(std::function<void(uint32_t)> &&cb) {
    this->pairing_complete_callback_.add(std::move(cb));
  }
  void add_on_pairing_mode_on_callback(std::function<void()> &&cb) {
    this->pairing_mode_on_callback_.add(std::move(cb));
  }
  void add_on_pairing_mode_off_callback(std::function<void()> &&cb) {
    this->pairing_mode_off_callback_.add(std::move(cb));
  }
  void add_on_state_change_callback(std::function<void(uint8_t)> &&cb) {
    this->state_change_callback_.add(std::move(cb));
  }
  void add_on_event_log_callback(std::function<void(nuki_log_entry_t)> &&cb) {
    this->event_log_callback_.add(std::move(cb));
    this->want_event_log_ = true;
  }
  void add_on_door_state_callback(std::function<void(uint8_t)> &&cb) {
    this->door_state_callback_.add(std::move(cb));
  }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  /* Operator actions (lambda-callable). */
  void pair();
  void unpair();
  /// pairing_mode switch semantics: PAIR now, retry every 5 s on failure,
  /// off after pairing_mode_timeout or PAIRING_COMPLETE.
  void set_pairing_mode(bool enabled);
  bool is_pairing_mode() const { return this->pairing_mode_; }
  void request_diagnostics();
  void request_lock_state();
  void request_config();
  void send_ping();
  void set_runtime_link_profile(uint8_t profile);
  void set_runtime_state_poll(uint16_t seconds);
  /* Secure link: TOFU pairing inside the bridge's window / forget it. */
  void pair_host();
  void unpair_host();
  bool is_secure() const { return nuki_seclink_ready(&this->sec_); }

  /* Extra lock actions (no PIN); `suffix` overrides the fixed name suffix
   * for this one action (<= 20 bytes, e.g. the Home Assistant user). */
  void lock_n_go(bool unlatch);
  void full_lock();
  void fob_action(int32_t n);
  bool lock_action(uint8_t cmd, const char *suffix = nullptr);
  /// Fixed name suffix for every following action (SET_ACTION_SUFFIX).
  void set_runtime_action_suffix(std::string suffix);

  /* PIN lifecycle. */
  void set_security_pin(uint32_t pin); // runtime override, persisted; 0 clears
  void verify_pin();                   // Verify Security PIN 0x0020
  PinState get_pin_state() const { return this->pin_state_; }
  /* Update Time 0x0021 from the `time:` component (local wall clock). */
  void update_time();
  /* Request Calibration 0x001A / Request Reboot 0x001D (PIN only). */
  void request_calibration();
  void request_reboot();

  /* Event log / keypad / authorization (payload commands, need the PIN).
   * Also exposed as Home Assistant services when api custom_services is on.
   */
  void request_event_logs(int32_t count);
  void request_auth_entries();
  void print_keypad_entries();
  void add_keypad_entry(std::string name, int32_t code);
  void update_keypad_entry(int32_t id, std::string name, int32_t code,
                           bool enabled);
  void delete_keypad_entry(int32_t id);

  bool is_paired() const { return this->paired_; }
  bool is_connected() const { return this->connected_; }
  bool is_link_ready() const { return this->link_ == LinkState::READY; }
  const BridgeDiagnostics &get_diagnostics() const { return this->diag_; }
  const nuki_config_t &get_config() const { return this->config_; }
  bool has_config() const { return this->config_valid_; }
  const char *get_last_unlock_user() const { return this->last_unlock_user_; }
  uint32_t get_last_log_index() const { return this->last_log_index_; }
  const char *get_auth_name(uint32_t auth_id) const;

protected:
  void control(const lock::LockCall &call) override;
  void open_latch() override;

  // ── TX ──
  bool send_hello_();
  uint16_t next_seq_();
  bool send_cmd_(uint8_t cmd, const uint8_t *data, size_t len,
                 uint16_t *seq_out = nullptr);
  bool send_action_(uint8_t cmd, lock::LockState optimistic,
                    const char *suffix = nullptr, bool is_retry = false);
  bool action_allowed_(uint8_t cmd);
  void send_action_suffix_();

  // ── RX ──
  void drain_rx_();
  void feed_rx_byte_(uint8_t b);
  void handle_frame_(const uint8_t *frame, size_t len);
  void dispatch_msg_(const nuki_uart_msg_t &msg);
  void handle_hello_(const nuki_uart_msg_t &msg);
  void handle_ack_(const nuki_uart_msg_t &msg);
  void handle_error_(const nuki_uart_msg_t &msg);
  void handle_status_(const nuki_uart_msg_t &msg);
  void handle_state_change_(const nuki_uart_msg_t &msg);
  void handle_error_report_(const nuki_uart_msg_t &msg);
  void handle_conn_status_(const nuki_uart_msg_t &msg);
  void handle_pairing_complete_(const nuki_uart_msg_t &msg);
  void handle_diagnostics_(const nuki_uart_msg_t &msg);
  void handle_config_(const nuki_uart_msg_t &msg);
  void handle_log_entry_(const nuki_uart_msg_t &msg);
  void handle_keypad_entry_(const nuki_uart_msg_t &msg);
  void handle_auth_entry_(const nuki_uart_msg_t &msg);
  bool handle_entry_status_(const nuki_uart_msg_t &msg, uint16_t cmd_id);
  void apply_keyturner_states_(const uint8_t *body, size_t len);
  void apply_door_sensor_(uint8_t state);
  void apply_battery_(const nuki_keyturner_t &k);
  void publish_door_security_state_();
  void publish_config_();

  // ── PIN lifecycle ──
  bool send_pin_only_cmd_(uint8_t cmd, uint16_t *seq_out);
  void load_pin_record_();
  void save_pin_record_();
  void set_pin_state_(PinState st, bool persist);
  void publish_pin_status_();
  void maybe_verify_pin_();
  void on_bad_pin_(uint8_t err, const char *what);
  static const char *pin_state_name(PinState st);

  // ── action robustness ──
  void arm_action_retry_(const char *why);
  void clear_action_retry_();
  void maybe_retry_action_();
  void maybe_update_time_();

  // ── entry streams / event log ──
  bool send_payload_cmd_(uint8_t cmd, const uint8_t *data, size_t len,
                         uint16_t *seq_out = nullptr);
  uint8_t pin_device_type_() const;
  bool pin_ready_(const char *what, bool allow_invalid = false) const;
  void begin_stream_(uint8_t cmd, uint16_t seq);
  void end_stream_(const char *why);
  void process_log_entry_(const nuki_log_entry_t &e);
  void fire_log_event_(const nuki_log_entry_t &e, const char *auth_name);
  void remember_auth_name_(uint32_t auth_id, const char *name);
  void schedule_log_poll_();
  void maybe_request_auth_entries_();
  void publish_last_unlock_user_(const char *name);
  bool want_logs_() const {
    return this->send_events_ || this->want_event_log_ ||
           this->last_unlock_user_sensor_ != nullptr;
  }
  static bool valid_keypad_name_(const std::string &name);
  static bool valid_keypad_code_(int32_t code);
  static bool valid_keypad_id_(int32_t id);

  // ── secure link ──
  bool sec_in_use_() const {
    return this->secure_link_ != SecureLinkMode::OFF && this->sec_available_ &&
           this->bridge_sec_supported_;
  }
  bool sec_ready_() const { return nuki_seclink_ready(&this->sec_); }
  bool send_sec_raw_(const uint8_t *body, size_t len);
  void start_handshake_();
  void handle_sec_frame_(const uint8_t *frame, size_t len);
  void on_sec_ready_();
  void sec_end_session_(const char *why);
  void run_link_ready_actions_();

  // ── bookkeeping ──
  void on_link_ready_();
  void on_bridge_restart_();
  void on_link_lost_();
  void set_connected_(bool connected);
  void set_paired_(bool paired);
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
  uint32_t pin_config_{0}; // YAML security_pin
  uint32_t pin_{0};        // effective PIN: override or YAML
  uint32_t pin_override_{0};
  PinState pin_state_{PinState::NOT_SET};
  uint32_t pairing_mode_timeout_s_{300};
  uint16_t allowed_actions_{ACTION_BIT_ALL};
  const char *action_suffix_{""};
  char action_suffix_buf_[NUKI_ACTION_SUFFIX_LEN + 1]{};
#ifdef USE_TIME
  time::RealTimeClock *time_{nullptr};
#endif
  uint8_t device_type_{NUKI_UART_DEVICE_AUTO};
  uint8_t id_type_{NUKI_UART_ID_TYPE_APP};
  uint8_t link_profile_{NUKI_UART_LINK_PROFILE_ARMED};
  uint32_t app_id_{0};
  uint32_t poll_interval_ms_{60000};
  bool auto_pair_{true};
  uint16_t state_poll_s_{60};
  SecureLinkMode secure_link_{SecureLinkMode::AUTO};
  uint8_t event_log_count_{5};
  const char *event_{"esphome.none"};
  bool send_events_{false};
  bool want_event_log_{false};

  binary_sensor::BinarySensor *connected_sensor_{nullptr};
  binary_sensor::BinarySensor *paired_sensor_{nullptr};
  binary_sensor::BinarySensor *door_sensor_{nullptr};
  binary_sensor::BinarySensor *tamper_sensor_{nullptr};
  binary_sensor::BinarySensor *battery_critical_sensor_{nullptr};
  binary_sensor::BinarySensor *battery_charging_sensor_{nullptr};
  binary_sensor::BinarySensor *keypad_battery_critical_sensor_{nullptr};
  binary_sensor::BinarySensor *door_sensor_battery_critical_sensor_{nullptr};
  binary_sensor::BinarySensor *night_mode_sensor_{nullptr};
  binary_sensor::BinarySensor *keypad_paired_sensor_{nullptr};
  sensor::Sensor *rssi_sensor_{nullptr};
  sensor::Sensor *battery_level_sensor_{nullptr};
  text_sensor::TextSensor *diagnostics_sensor_{nullptr};
  text_sensor::TextSensor *last_unlock_user_sensor_{nullptr};
  text_sensor::TextSensor *last_lock_action_sensor_{nullptr};
  text_sensor::TextSensor *last_lock_action_trigger_sensor_{nullptr};
  text_sensor::TextSensor *completion_status_sensor_{nullptr};
  text_sensor::TextSensor *door_sensor_state_sensor_{nullptr};
  text_sensor::TextSensor *door_security_state_sensor_{nullptr};
  text_sensor::TextSensor *pin_status_sensor_{nullptr};
  text_sensor::TextSensor *nuki_state_sensor_{nullptr};
  text_sensor::TextSensor *firmware_version_sensor_{nullptr};
  text_sensor::TextSensor *hardware_version_sensor_{nullptr};
  text_sensor::TextSensor *lock_name_sensor_{nullptr};
  text_sensor::TextSensor *nuki_id_sensor_{nullptr};
  switch_::Switch *pairing_mode_switch_{nullptr};
  CallbackManager<void(uint32_t)> pairing_complete_callback_;
  CallbackManager<void()> pairing_mode_on_callback_;
  CallbackManager<void()> pairing_mode_off_callback_;
  CallbackManager<void(uint8_t)> state_change_callback_;
  CallbackManager<void(nuki_log_entry_t)> event_log_callback_;
  CallbackManager<void(uint8_t)> door_state_callback_;

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
  uint32_t action_watchdog_ms_{0};
  uint8_t action_cmd_{0};
  uint16_t pair_seq_{0};
  uint32_t pair_sent_ms_{0};
  bool pair_attempted_{false};
  uint16_t verify_seq_{0};
  uint16_t time_seq_{0};
  uint16_t config_seq_{0};
  uint16_t suffix_seq_{0};
  // one bounded retry of an action that never reached the lock
  uint8_t retry_cmd_{0};
  lock::LockState retry_optimistic_{lock::LOCK_STATE_NONE};
  uint32_t retry_deadline_ms_{0};
  char retry_suffix_[NUKI_ACTION_SUFFIX_LEN + 1]{};
  uint32_t actions_ignored_until_ms_{0};
  bool api_was_connected_{false};

  // lock / bridge state
  bool paired_{false};
  bool pairing_{false};
  bool pairing_mode_{false};
  bool connected_{false};
  uint8_t conn_state_{NUKI_UART_CONN_IDLE};
  uint8_t last_nuki_lock_state_{NUKI_LOCK_STATE_UNDEFINED};
  int16_t nuki_state_{-1}; // Keyturner States byte 0, -1 = none
  int8_t rssi_dbm_{0};
  bool rssi_valid_{false};
  BridgeDiagnostics diag_{};
  int16_t door_state_{-1}; // last Keyturner States door byte, -1 = none
  int16_t last_action_{-1};
  int16_t last_action_trigger_{-1};
  int16_t last_action_completion_{-1};
  int16_t battery_percent_{-1};
  int16_t config_update_count_{-1};
  nuki_config_t config_{};
  bool config_valid_{false};
  bool verify_unsupported_{false}; // bridge answered UNKNOWN_CMD to 0x14
  bool suffix_unsupported_{false}; // same for 0x15
  bool verify_after_connect_{false};
  bool time_update_due_{false};

  // entry stream in progress (0x30 / 0x40 / 0x51), see begin_stream_()
  uint8_t stream_cmd_{0};
  uint16_t stream_seq_{0};
  uint32_t stream_last_ms_{0};
  uint16_t stream_entries_{0};

  // event log
  uint32_t last_log_index_{0};  // newest index ever processed
  uint32_t log_batch_floor_{0}; // entries above this fire events
  uint32_t last_user_index_{0}; // index that set last_unlock_user_
  char last_unlock_user_[NUKI_AUTH_NAME_LEN + 1]{};
  bool log_poll_scheduled_{false};

  // authorization names
  std::array<AuthName, AUTH_NAME_SLOTS> auth_names_{};
  uint32_t auth_fetched_ms_{0};
  bool auth_fetched_{false};

  // secure link
  nuki_seclink_ctx_t sec_{};
  NukiSecLinkKeyStore keys_;
  bool sec_available_{false};        // static key loaded, libsodium up
  bool bridge_sec_supported_{false}; // HELLO caps 0x0020
  bool bridge_sec_required_{false};  // HELLO caps 0x0040
  uint32_t hs_sent_ms_{0};
  uint8_t hs_attempts_{0};
  bool host_pairing_{false};
  uint32_t host_pair_sent_ms_{0};
  bool link_actions_deferred_{false};
  uint32_t sec_open_failures_{0};
  std::array<uint8_t, SEC_SCRATCH_LEN> sec_scratch_{};  // opened inner frames
  std::array<uint8_t, NUKI_UART_FRAME_MAX> sec_seal_{}; // sealed TX bodies
};

/* ── Sub-entities (switch / buttons), same shape as the BLE component ── */

class NukiUartBridgePairingModeSwitch : public switch_::Switch,
                                        public Parented<NukiUartBridgeLock> {
protected:
  void write_state(bool state) override {
    this->parent_->set_pairing_mode(state);
  }
};

class NukiUartBridgeUnpairButton : public button::Button,
                                   public Parented<NukiUartBridgeLock> {
protected:
  void press_action() override { this->parent_->unpair(); }
};

class NukiUartBridgeCalibrationButton : public button::Button,
                                        public Parented<NukiUartBridgeLock> {
protected:
  void press_action() override { this->parent_->request_calibration(); }
};

class NukiUartBridgeRebootButton : public button::Button,
                                   public Parented<NukiUartBridgeLock> {
protected:
  void press_action() override { this->parent_->request_reboot(); }
};

} // namespace nuki_uart_bridge
} // namespace esphome
