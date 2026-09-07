#pragma once

/*
 * Host-side builders and parsers for the Nuki "entry" commands that travel
 * through the nRF52840 bridge as pass-through payloads:
 *
 *   Request Log Entries 0x0031 / Log Entry 0x0032 / Log Entry Count 0x0033
 *   Add / Request / Update / Remove Keypad Code 0x0041 / 0x0043 / 0x0046 /
 * 0x0047 Keypad Code ID 0x0042 / Keypad Code Count 0x0044 / Keypad Code 0x0045
 *   Request Authorization Entries 0x0009 / Authorization Entry 0x000A /
 *   Authorization Entry Count 0x0027
 *   Verify Security PIN 0x0020 / Update Time 0x0021 / Request Calibration
 *   0x001A / Request Reboot 0x001D (PIN-terminated, no other fields)
 *   Request Config 0x0014 / Config 0x0015 and the Keyturner States 0x000C
 *   body (length-driven parsers, spec pp.30-35 and pp.42-46)
 *
 * Field layouts follow Nuki BLE API v2.3.1 (page numbers in the comments);
 * the UART placement rules follow docs/host-integration.md §7 of the bridge:
 * the host sends the spec's fields minus nK, with the Security-PIN last, and
 * the PIN container is uint16 on gen 1-4 and uint32 on Ultra (spec p.16).
 *
 * Like nuki_uart_framing.h this header is dependency-free C99 that is also
 * valid C++, so tests/test_uart_entries.c compiles it on the host.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "nuki_uart_framing.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Additional protocol constants ───────────────────────────────────── */

/* Host -> bridge payload commands (src/nuki_command.h on the bridge) */
#define NUKI_UART_CMD_REQ_AUTH_ENTRIES 0x30
#define NUKI_UART_CMD_REQ_LOG_ENTRIES 0x40
#define NUKI_UART_CMD_ADD_KEYPAD 0x50
#define NUKI_UART_CMD_REQ_KEYPAD_CODES 0x51
#define NUKI_UART_CMD_UPDATE_KEYPAD 0x53
#define NUKI_UART_CMD_REMOVE_KEYPAD 0x54
/* SET_STATE_POLL: [seconds:2 LE], cadence of the bridge's own state read */
#define NUKI_UART_CMD_SET_STATE_POLL 0x7A

/* Bridge -> host bulk entry streams */
#define NUKI_UART_RSP_AUTH_ENTRY 0x87
#define NUKI_UART_RSP_LOG_ENTRY 0x88
#define NUKI_UART_RSP_KEYPAD_ENTRY 0x89

/* Nuki command ids carried as [cmd_id:2 LE][payload] */
#define NUKI_CMD_ID_AUTH_ENTRY 0x000A
#define NUKI_CMD_ID_AUTH_ENTRY_COUNT 0x0027
#define NUKI_CMD_ID_LOG_ENTRY 0x0032
#define NUKI_CMD_ID_LOG_ENTRY_COUNT 0x0033
#define NUKI_CMD_ID_KEYPAD_CODE_ID 0x0042
#define NUKI_CMD_ID_KEYPAD_CODE_COUNT 0x0044
#define NUKI_CMD_ID_KEYPAD_CODE 0x0045

/* Log Entry types (spec pp.50-52) */
#define NUKI_LOG_TYPE_LOGGING_ENABLED 0x01
#define NUKI_LOG_TYPE_LOCK_ACTION 0x02
#define NUKI_LOG_TYPE_CALIBRATION 0x03
#define NUKI_LOG_TYPE_INIT_RUN 0x04
#define NUKI_LOG_TYPE_KEYPAD_ACTION 0x05
#define NUKI_LOG_TYPE_DOOR_SENSOR 0x06
#define NUKI_LOG_TYPE_DOOR_SENSOR_LOGGING 0x07

/* Completion status (spec pp.51-52) */
#define NUKI_COMPLETION_SUCCESS 0x00
#define NUKI_COMPLETION_INVALID_CODE 0xE0
#define NUKI_COMPLETION_INVALID_FINGERPRINT 0xE1

/* Keyturner States trigger (spec p.31) */
#define NUKI_TRIGGER_SYSTEM 0x00
#define NUKI_TRIGGER_MANUAL 0x01
#define NUKI_TRIGGER_BUTTON 0x02
#define NUKI_TRIGGER_AUTOMATIC 0x03
#define NUKI_TRIGGER_AUTO_LOCK 0x06

/* Door sensor state (spec p.32; the two generation tables do not overlap) */
#define NUKI_DOOR_UNAVAILABLE 0x00
#define NUKI_DOOR_DEACTIVATED 0x01
#define NUKI_DOOR_CLOSED 0x02
#define NUKI_DOOR_OPENED 0x03
#define NUKI_DOOR_STATE_UNKNOWN 0x04
#define NUKI_DOOR_CALIBRATING 0x05
#define NUKI_DOOR_UNCALIBRATED 0x10
#define NUKI_DOOR_TAMPERED 0xF0
#define NUKI_DOOR_UNKNOWN 0xFF

/* Sizes */
#define NUKI_AUTH_NAME_LEN 32
#define NUKI_KEYPAD_NAME_LEN 20
#define NUKI_LOG_DATA_MAX 5
#define NUKI_TS_LEN 7
#define NUKI_UART_PAYLOAD_MAX 96 /* CMD_ENGINE_MAX_PAYLOAD on the bridge */

/* Sort order for Request Log Entries */
#define NUKI_LOG_SORT_ASCENDING 0x00
#define NUKI_LOG_SORT_DESCENDING 0x01

/* ── Timestamps (uint8[7]: year LE16, month, day, hour, minute, second) ── */

typedef struct {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
} nuki_ts_t;

static inline void nuki_uart_get_ts(const uint8_t *p, nuki_ts_t *ts) {
  ts->year = nuki_uart_get_u16(p);
  ts->month = p[2];
  ts->day = p[3];
  ts->hour = p[4];
  ts->minute = p[5];
  ts->second = p[6];
}

static inline void nuki_uart_put_ts(uint8_t *p, const nuki_ts_t *ts) {
  p[0] = (uint8_t)ts->year;
  p[1] = (uint8_t)(ts->year >> 8);
  p[2] = ts->month;
  p[3] = ts->day;
  p[4] = ts->hour;
  p[5] = ts->minute;
  p[6] = ts->second;
}

static inline void nuki_uart_put_u16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

/*
 * Copy a fixed-width, NUL-padded Nuki name into a C string.  Stops at the
 * first NUL or at src_len, always terminates.  dst_size must be >= 1.
 */
static inline void nuki_uart_copy_name(char *dst, size_t dst_size,
                                       const uint8_t *src, size_t src_len) {
  size_t n = 0;
  while (n < src_len && n + 1U < dst_size && src[n] != 0U) {
    dst[n] = (char)src[n];
    n++;
  }
  dst[n] = '\0';
}

/* Fixed-width name field: NUL padded, silently truncated. */
static inline void nuki_uart_put_name(uint8_t *dst, size_t width,
                                      const char *src) {
  size_t n = 0;
  memset(dst, 0, width);
  if (src == NULL) {
    return;
  }
  while (n < width && src[n] != '\0') {
    dst[n] = (uint8_t)src[n];
    n++;
  }
}

/* ── Security PIN ─────────────────────────────────────────────────────── */

/* Container width for `device_type` (spec p.16): 4 on Ultra, else 2. */
static inline size_t nuki_uart_pin_width(uint8_t device_type) {
  return device_type == NUKI_UART_DEVICE_ULTRA ? 4U : 2U;
}

/* Append the PIN in the width the lock expects.  Returns bytes written. */
static inline size_t nuki_uart_put_pin(uint8_t *out, uint32_t pin,
                                       uint8_t device_type) {
  if (nuki_uart_pin_width(device_type) == 4U) {
    nuki_uart_put_u32(out, pin);
    return 4U;
  }
  nuki_uart_put_u16(out, (uint16_t)pin);
  return 2U;
}

/* ── Request builders (payload after the UART command byte) ──────────── */

/*
 * Request Log Entries 0x0031 (spec pp.49-50):
 *   start index(4) count(2) sort order(1) total count flag(1) [nK] PIN
 * Returns the payload length (8 + PIN width).
 */
static inline size_t nuki_uart_build_req_log_entries(
    uint8_t *out, uint32_t start_index, uint16_t count, uint8_t sort_order,
    uint8_t total_count, uint32_t pin, uint8_t device_type) {
  size_t n = 0;
  nuki_uart_put_u32(out + n, start_index);
  n += 4;
  nuki_uart_put_u16(out + n, count);
  n += 2;
  out[n++] = sort_order;
  out[n++] = total_count;
  n += nuki_uart_put_pin(out + n, pin, device_type);
  return n;
}

/*
 * Request Keypad Codes 0x0043 (spec p.66): offset(2) count(2) [nK] PIN.
 * Returns 4 + PIN width.
 */
static inline size_t
nuki_uart_build_req_keypad_codes(uint8_t *out, uint16_t offset, uint16_t count,
                                 uint32_t pin, uint8_t device_type) {
  size_t n = 0;
  nuki_uart_put_u16(out + n, offset);
  n += 2;
  nuki_uart_put_u16(out + n, count);
  n += 2;
  n += nuki_uart_put_pin(out + n, pin, device_type);
  return n;
}

/*
 * Request Authorization Entries 0x0009 (spec p.22): offset(2) count(2)
 * [nK] PIN.  The optional ID-type filter is not sent (gen 4 / Ultra only
 * and the bridge inserts nK right after `count`).  Returns 4 + PIN width.
 */
static inline size_t
nuki_uart_build_req_auth_entries(uint8_t *out, uint16_t offset, uint16_t count,
                                 uint32_t pin, uint8_t device_type) {
  return nuki_uart_build_req_keypad_codes(out, offset, count, pin, device_type);
}

/* Time restriction block shared by Add / Update / Keypad Code (19 bytes). */
typedef struct {
  nuki_ts_t allowed_from;
  nuki_ts_t allowed_until;
  uint8_t weekdays; /* bit6 MO .. bit0 SU; 0 = every day */
  uint8_t from_hour;
  uint8_t from_minute;
  uint8_t until_hour;
  uint8_t until_minute;
} nuki_keypad_limits_t;

#define NUKI_KEYPAD_LIMITS_LEN 19

static inline size_t nuki_uart_put_limits(uint8_t *out,
                                          const nuki_keypad_limits_t *lim) {
  if (lim == NULL) {
    memset(out, 0, NUKI_KEYPAD_LIMITS_LEN);
    return NUKI_KEYPAD_LIMITS_LEN;
  }
  nuki_uart_put_ts(out, &lim->allowed_from);
  nuki_uart_put_ts(out + 7, &lim->allowed_until);
  out[14] = lim->weekdays;
  out[15] = lim->from_hour;
  out[16] = lim->from_minute;
  out[17] = lim->until_hour;
  out[18] = lim->until_minute;
  return NUKI_KEYPAD_LIMITS_LEN;
}

static inline void nuki_uart_get_limits(const uint8_t *p,
                                        nuki_keypad_limits_t *lim) {
  nuki_uart_get_ts(p, &lim->allowed_from);
  nuki_uart_get_ts(p + 7, &lim->allowed_until);
  lim->weekdays = p[14];
  lim->from_hour = p[15];
  lim->from_minute = p[16];
  lim->until_hour = p[17];
  lim->until_minute = p[18];
}

/*
 * Add Keypad Code 0x0041 (spec pp.63-65):
 *   code(4) name(20) time limited(1) allowed from(7) allowed until(7)
 *   weekdays(1) from time(2) until time(2) [nK] PIN
 * Every field is mandatory, so the restriction block is sent zeroed when
 * `limits` is NULL (as the reference client does).  Returns 44 + PIN width.
 */
static inline size_t
nuki_uart_build_add_keypad(uint8_t *out, uint32_t code, const char *name,
                           const nuki_keypad_limits_t *limits, uint32_t pin,
                           uint8_t device_type) {
  size_t n = 0;
  nuki_uart_put_u32(out + n, code);
  n += 4;
  nuki_uart_put_name(out + n, NUKI_KEYPAD_NAME_LEN, name);
  n += NUKI_KEYPAD_NAME_LEN;
  out[n++] = limits != NULL ? 1U : 0U;
  n += nuki_uart_put_limits(out + n, limits);
  n += nuki_uart_put_pin(out + n, pin, device_type);
  return n;
}

/*
 * Update Keypad Code 0x0046 (spec pp.70-72):
 *   code id(2) code(4) name(20) enabled(1) time limited(1) <limits 19>
 *   [nK] PIN.  Returns 47 + PIN width.
 */
static inline size_t
nuki_uart_build_update_keypad(uint8_t *out, uint16_t code_id, uint32_t code,
                              const char *name, uint8_t enabled,
                              const nuki_keypad_limits_t *limits, uint32_t pin,
                              uint8_t device_type) {
  size_t n = 0;
  nuki_uart_put_u16(out + n, code_id);
  n += 2;
  nuki_uart_put_u32(out + n, code);
  n += 4;
  nuki_uart_put_name(out + n, NUKI_KEYPAD_NAME_LEN, name);
  n += NUKI_KEYPAD_NAME_LEN;
  out[n++] = enabled ? 1U : 0U;
  out[n++] = limits != NULL ? 1U : 0U;
  n += nuki_uart_put_limits(out + n, limits);
  n += nuki_uart_put_pin(out + n, pin, device_type);
  return n;
}

/* Remove Keypad Code 0x0047 (spec p.73): code id(2) [nK] PIN. */
static inline size_t nuki_uart_build_remove_keypad(uint8_t *out,
                                                   uint16_t code_id,
                                                   uint32_t pin,
                                                   uint8_t device_type) {
  size_t n = 0;
  nuki_uart_put_u16(out + n, code_id);
  n += 2;
  n += nuki_uart_put_pin(out + n, pin, device_type);
  return n;
}

/* SET_STATE_POLL 0x7A: [seconds:2 LE]. */
static inline size_t nuki_uart_build_set_state_poll(uint8_t *out,
                                                    uint16_t seconds) {
  nuki_uart_put_u16(out, seconds);
  return 2U;
}

/*
 * PIN-only commands: Verify Security PIN 0x0020 (p.47, UART 0x14), Request
 * Calibration 0x001A (p.47, UART 0x70) and Request Reboot 0x001D (p.47-48,
 * UART 0x73).  The bridge inserts nK in front of the PIN.  Returns the PIN
 * width (2 or 4).
 */
static inline size_t nuki_uart_build_pin_only(uint8_t *out, uint32_t pin,
                                              uint8_t device_type) {
  return nuki_uart_put_pin(out, pin, device_type);
}

/*
 * Update Time 0x0021 (p.48, UART 0x13): time(7) [nK] PIN.  Returns 7 + PIN
 * width.  The lock keeps *local* time (Keyturner States carries the
 * timezone offset separately), so callers pass the wall clock.
 */
static inline size_t nuki_uart_build_update_time(uint8_t *out,
                                                 const nuki_ts_t *ts,
                                                 uint32_t pin,
                                                 uint8_t device_type) {
  size_t n = 0;
  nuki_uart_put_ts(out + n, ts);
  n += NUKI_TS_LEN;
  n += nuki_uart_put_pin(out + n, pin, device_type);
  return n;
}

/* Lock Action name suffix (spec p.36): at most 20 bytes, no padding. */
#define NUKI_ACTION_SUFFIX_LEN 20

/*
 * SET_ACTION_SUFFIX 0x15 payload and the optional trailing payload of the
 * action commands: the suffix bytes without NUL and without padding.  An
 * empty name gives an empty payload (= clear).  Returns the length (0..20).
 */
static inline size_t nuki_uart_build_action_suffix(uint8_t *out,
                                                   const char *name) {
  size_t n = 0;
  if (name == NULL) {
    return 0U;
  }
  while (n < NUKI_ACTION_SUFFIX_LEN && name[n] != '\0') {
    out[n] = (uint8_t)name[n];
    n++;
  }
  return n;
}

/* ── Entry parsers (input = UART DATA: [cmd_id:2 LE][payload]) ───────── */

/* Log Entry 0x0032 (spec pp.50-52). */
typedef struct {
  uint32_t index;
  nuki_ts_t ts;
  uint32_t auth_id;
  char name[NUKI_AUTH_NAME_LEN + 1];
  uint8_t type;
  uint8_t data[NUKI_LOG_DATA_MAX];
  uint8_t data_len; /* bytes actually present, capped at NUKI_LOG_DATA_MAX */
} nuki_log_entry_t;

/* Spec'd data length for a log type; 0 for unknown types. */
static inline uint8_t nuki_log_data_len(uint8_t type) {
  switch (type) {
  case NUKI_LOG_TYPE_LOGGING_ENABLED:
  case NUKI_LOG_TYPE_DOOR_SENSOR:
  case NUKI_LOG_TYPE_DOOR_SENSOR_LOGGING:
    return 1U;
  case NUKI_LOG_TYPE_LOCK_ACTION:
  case NUKI_LOG_TYPE_CALIBRATION:
  case NUKI_LOG_TYPE_INIT_RUN:
    return 4U;
  case NUKI_LOG_TYPE_KEYPAD_ACTION:
    return 5U;
  default:
    return 0U;
  }
}

#define NUKI_LOG_ENTRY_MIN_LEN                                                 \
  (2U + 4U + NUKI_TS_LEN + 4U + NUKI_AUTH_NAME_LEN + 1U)

static inline int nuki_uart_parse_log_entry(const uint8_t *data, size_t len,
                                            nuki_log_entry_t *e) {
  size_t o = 2;
  size_t rest;

  if (data == NULL || e == NULL) {
    return NUKI_UART_ERR_INVALID;
  }
  if (len < 2U || nuki_uart_get_u16(data) != NUKI_CMD_ID_LOG_ENTRY) {
    return NUKI_UART_ERR_INVALID;
  }
  if (len < NUKI_LOG_ENTRY_MIN_LEN) {
    return NUKI_UART_ERR_SHORT;
  }
  memset(e, 0, sizeof(*e));
  e->index = nuki_uart_get_u32(data + o);
  o += 4;
  nuki_uart_get_ts(data + o, &e->ts);
  o += NUKI_TS_LEN;
  e->auth_id = nuki_uart_get_u32(data + o);
  o += 4;
  nuki_uart_copy_name(e->name, sizeof(e->name), data + o, NUKI_AUTH_NAME_LEN);
  o += NUKI_AUTH_NAME_LEN;
  e->type = data[o++];
  /* Length-driven: a newer lock may append bytes, an older one may omit. */
  rest = len - o;
  if (rest > NUKI_LOG_DATA_MAX) {
    rest = NUKI_LOG_DATA_MAX;
  }
  memcpy(e->data, data + o, rest);
  e->data_len = (uint8_t)rest;
  return NUKI_UART_OK;
}

/* Keypad code id of a type 0x05 entry (bytes 4-5 of data), 0 if absent. */
static inline uint16_t nuki_log_entry_code_id(const nuki_log_entry_t *e) {
  if (e->type != NUKI_LOG_TYPE_KEYPAD_ACTION || e->data_len < 5U) {
    return 0U;
  }
  return nuki_uart_get_u16(e->data + 3);
}

/* Log Entry Count 0x0033 (spec p.53). */
typedef struct {
  uint8_t logging_enabled;
  uint16_t count;
  uint8_t door_sensor_enabled;
  uint8_t door_sensor_logging_enabled;
} nuki_log_count_t;

static inline int nuki_uart_parse_log_count(const uint8_t *data, size_t len,
                                            nuki_log_count_t *c) {
  if (data == NULL || c == NULL) {
    return NUKI_UART_ERR_INVALID;
  }
  if (len < 2U || nuki_uart_get_u16(data) != NUKI_CMD_ID_LOG_ENTRY_COUNT) {
    return NUKI_UART_ERR_INVALID;
  }
  if (len < 5U) {
    return NUKI_UART_ERR_SHORT;
  }
  c->logging_enabled = data[2];
  c->count = nuki_uart_get_u16(data + 3);
  c->door_sensor_enabled = len > 5U ? data[5] : 0U;
  c->door_sensor_logging_enabled = len > 6U ? data[6] : 0U;
  return NUKI_UART_OK;
}

/* Keypad Code 0x0045 (spec pp.67-69): tail only when time limited. */
typedef struct {
  uint16_t code_id;
  uint32_t code;
  char name[NUKI_KEYPAD_NAME_LEN + 1];
  uint8_t enabled;
  nuki_ts_t created;
  nuki_ts_t last_active;
  uint16_t lock_count;
  uint8_t time_limited;
  nuki_keypad_limits_t limits; /* valid when time_limited != 0 */
} nuki_keypad_code_t;

#define NUKI_KEYPAD_CODE_BASE_LEN                                              \
  (2U + 2U + 4U + NUKI_KEYPAD_NAME_LEN + 1U + NUKI_TS_LEN + NUKI_TS_LEN + 2U + \
   1U)

static inline int nuki_uart_parse_keypad_code(const uint8_t *data, size_t len,
                                              nuki_keypad_code_t *e) {
  size_t o = 2;

  if (data == NULL || e == NULL) {
    return NUKI_UART_ERR_INVALID;
  }
  if (len < 2U || nuki_uart_get_u16(data) != NUKI_CMD_ID_KEYPAD_CODE) {
    return NUKI_UART_ERR_INVALID;
  }
  if (len < NUKI_KEYPAD_CODE_BASE_LEN) {
    return NUKI_UART_ERR_SHORT;
  }
  memset(e, 0, sizeof(*e));
  e->code_id = nuki_uart_get_u16(data + o);
  o += 2;
  e->code = nuki_uart_get_u32(data + o);
  o += 4;
  nuki_uart_copy_name(e->name, sizeof(e->name), data + o, NUKI_KEYPAD_NAME_LEN);
  o += NUKI_KEYPAD_NAME_LEN;
  e->enabled = data[o++];
  nuki_uart_get_ts(data + o, &e->created);
  o += NUKI_TS_LEN;
  nuki_uart_get_ts(data + o, &e->last_active);
  o += NUKI_TS_LEN;
  e->lock_count = nuki_uart_get_u16(data + o);
  o += 2;
  e->time_limited = data[o++];
  if (e->time_limited != 0U) {
    if (len < o + NUKI_KEYPAD_LIMITS_LEN) {
      return NUKI_UART_ERR_SHORT;
    }
    nuki_uart_get_limits(data + o, &e->limits);
  }
  return NUKI_UART_OK;
}

/* Keypad Code Count 0x0044 (spec p.66). */
static inline int nuki_uart_parse_keypad_count(const uint8_t *data, size_t len,
                                               uint16_t *count) {
  if (data == NULL || count == NULL) {
    return NUKI_UART_ERR_INVALID;
  }
  if (len < 2U || nuki_uart_get_u16(data) != NUKI_CMD_ID_KEYPAD_CODE_COUNT) {
    return NUKI_UART_ERR_INVALID;
  }
  if (len < 4U) {
    return NUKI_UART_ERR_SHORT;
  }
  *count = nuki_uart_get_u16(data + 2);
  return NUKI_UART_OK;
}

/* Keypad Code ID 0x0042 (spec p.65): reply to Add Keypad Code. */
static inline int nuki_uart_parse_keypad_code_id(const uint8_t *data,
                                                 size_t len, uint16_t *code_id,
                                                 nuki_ts_t *created) {
  if (data == NULL || code_id == NULL) {
    return NUKI_UART_ERR_INVALID;
  }
  if (len < 2U || nuki_uart_get_u16(data) != NUKI_CMD_ID_KEYPAD_CODE_ID) {
    return NUKI_UART_ERR_INVALID;
  }
  if (len < 4U) {
    return NUKI_UART_ERR_SHORT;
  }
  *code_id = nuki_uart_get_u16(data + 2);
  if (created != NULL) {
    memset(created, 0, sizeof(*created));
    if (len >= 4U + NUKI_TS_LEN) {
      nuki_uart_get_ts(data + 4, created);
    }
  }
  return NUKI_UART_OK;
}

/* Authorization Entry 0x000A (spec pp.23-25). */
typedef struct {
  uint32_t auth_id;
  uint8_t id_type; /* 0 App, 1 Bridge, 2 Fob, 3 Keypad */
  char name[NUKI_AUTH_NAME_LEN + 1];
  uint8_t enabled;
  uint8_t remote_allowed;
  uint8_t has_details; /* created / last_active / lock_count present */
  nuki_ts_t created;
  nuki_ts_t last_active;
  uint16_t lock_count;
  uint8_t time_limited;
} nuki_auth_entry_t;

#define NUKI_AUTH_ENTRY_MIN_LEN (2U + 4U + 1U + NUKI_AUTH_NAME_LEN + 1U + 1U)
#define NUKI_AUTH_ENTRY_DETAILS_LEN                                            \
  (NUKI_AUTH_ENTRY_MIN_LEN + NUKI_TS_LEN + NUKI_TS_LEN + 2U + 1U)

static inline int nuki_uart_parse_auth_entry(const uint8_t *data, size_t len,
                                             nuki_auth_entry_t *e) {
  size_t o = 2;

  if (data == NULL || e == NULL) {
    return NUKI_UART_ERR_INVALID;
  }
  if (len < 2U || nuki_uart_get_u16(data) != NUKI_CMD_ID_AUTH_ENTRY) {
    return NUKI_UART_ERR_INVALID;
  }
  if (len < NUKI_AUTH_ENTRY_MIN_LEN) {
    return NUKI_UART_ERR_SHORT;
  }
  memset(e, 0, sizeof(*e));
  e->auth_id = nuki_uart_get_u32(data + o);
  o += 4;
  e->id_type = data[o++];
  nuki_uart_copy_name(e->name, sizeof(e->name), data + o, NUKI_AUTH_NAME_LEN);
  o += NUKI_AUTH_NAME_LEN;
  e->enabled = data[o++];
  e->remote_allowed = data[o++];
  if (len >= NUKI_AUTH_ENTRY_DETAILS_LEN) {
    e->has_details = 1U;
    nuki_uart_get_ts(data + o, &e->created);
    o += NUKI_TS_LEN;
    nuki_uart_get_ts(data + o, &e->last_active);
    o += NUKI_TS_LEN;
    e->lock_count = nuki_uart_get_u16(data + o);
    o += 2;
    e->time_limited = data[o++];
  }
  return NUKI_UART_OK;
}

/* Authorization Entry Count 0x0027 (spec p.49). */
static inline int nuki_uart_parse_auth_count(const uint8_t *data, size_t len,
                                             uint16_t *count) {
  if (data == NULL || count == NULL) {
    return NUKI_UART_ERR_INVALID;
  }
  if (len < 2U || nuki_uart_get_u16(data) != NUKI_CMD_ID_AUTH_ENTRY_COUNT) {
    return NUKI_UART_ERR_INVALID;
  }
  if (len < 4U) {
    return NUKI_UART_ERR_SHORT;
  }
  *count = nuki_uart_get_u16(data + 2);
  return NUKI_UART_OK;
}

/* ── Keyturner States 0x000C (spec pp.30-35) ─────────────────────────── */

/*
 * Byte offsets inside the payload.  The 2016 worked example (p.87 step 2e)
 * stops after the battery byte (13 bytes); every later field is optional
 * and reported through the has_* flags.
 */
#define NUKI_KS_OFF_NUKI_STATE 0
#define NUKI_KS_OFF_LOCK_STATE 1
#define NUKI_KS_OFF_TRIGGER 2
#define NUKI_KS_OFF_TIME 3
#define NUKI_KS_OFF_TZ_OFFSET 10
#define NUKI_KS_OFF_BATTERY 12
#define NUKI_KS_OFF_CONFIG_UPDATE_COUNT 13
#define NUKI_KS_OFF_LOCK_N_GO_TIMER 14
#define NUKI_KS_OFF_LAST_ACTION 15
#define NUKI_KS_OFF_LAST_ACTION_TRIGGER 16
#define NUKI_KS_OFF_LAST_ACTION_COMPLETION 17
#define NUKI_KS_OFF_DOOR_SENSOR 18
#define NUKI_KS_OFF_NIGHT_MODE 19
#define NUKI_KS_OFF_ACCESSORY_BATTERY 20
#define NUKI_KS_OFF_REMOTE_ACCESS 21

typedef struct {
  uint8_t nuki_state;
  uint8_t lock_state;
  uint8_t trigger; /* 0xFF when absent */
  nuki_ts_t time;  /* valid when has_time */
  int16_t tz_offset_min;
  uint8_t has_time;
  uint8_t has_battery;
  uint8_t battery_critical;
  uint8_t battery_charging;
  uint8_t battery_percent; /* 0..100 in steps of 2 */
  uint8_t has_config_update_count;
  uint8_t config_update_count;
  uint8_t lock_n_go_timer;
  uint8_t has_last_action;
  uint8_t last_action;
  uint8_t last_action_trigger;
  uint8_t has_last_action_completion;
  uint8_t last_action_completion;
  uint8_t has_door_sensor;
  uint8_t door_sensor;
  uint8_t has_night_mode;
  uint8_t night_mode;
  uint8_t has_accessory_battery;
  uint8_t keypad_present;
  uint8_t keypad_battery_critical;
  uint8_t door_sensor_battery_present;
  uint8_t door_sensor_battery_critical;
  uint8_t has_remote_access;
  uint8_t remote_access; /* raw bitmask, p.33 */
} nuki_keyturner_t;

/* Input = the body after the [0C 00] command id (nuki_uart_keyturner_body). */
static inline int nuki_uart_parse_keyturner(const uint8_t *body, size_t len,
                                            nuki_keyturner_t *k) {
  if (body == NULL || k == NULL) {
    return NUKI_UART_ERR_INVALID;
  }
  if (len < 2U) {
    return NUKI_UART_ERR_SHORT;
  }
  memset(k, 0, sizeof(*k));
  k->trigger = 0xFF;
  k->nuki_state = body[NUKI_KS_OFF_NUKI_STATE];
  k->lock_state = body[NUKI_KS_OFF_LOCK_STATE];
  if (len > NUKI_KS_OFF_TRIGGER) {
    k->trigger = body[NUKI_KS_OFF_TRIGGER];
  }
  if (len >= NUKI_KS_OFF_TZ_OFFSET + 2U) {
    k->has_time = 1U;
    nuki_uart_get_ts(body + NUKI_KS_OFF_TIME, &k->time);
    k->tz_offset_min = (int16_t)nuki_uart_get_u16(body + NUKI_KS_OFF_TZ_OFFSET);
  }
  if (len > NUKI_KS_OFF_BATTERY) {
    const uint8_t b = body[NUKI_KS_OFF_BATTERY];
    k->has_battery = 1U;
    k->battery_critical = (b & NUKI_BATTERY_CRITICAL_BIT) != 0U;
    k->battery_charging = (b & NUKI_BATTERY_CHARGING_BIT) != 0U;
    k->battery_percent = (uint8_t)(((b >> 2) & 0x3FU) * 2U);
  }
  if (len > NUKI_KS_OFF_CONFIG_UPDATE_COUNT) {
    k->has_config_update_count = 1U;
    k->config_update_count = body[NUKI_KS_OFF_CONFIG_UPDATE_COUNT];
  }
  if (len > NUKI_KS_OFF_LOCK_N_GO_TIMER) {
    k->lock_n_go_timer = body[NUKI_KS_OFF_LOCK_N_GO_TIMER];
  }
  if (len > NUKI_KS_OFF_LAST_ACTION_TRIGGER) {
    k->has_last_action = 1U;
    k->last_action = body[NUKI_KS_OFF_LAST_ACTION];
    k->last_action_trigger = body[NUKI_KS_OFF_LAST_ACTION_TRIGGER];
  }
  if (len > NUKI_KS_OFF_LAST_ACTION_COMPLETION) {
    k->has_last_action_completion = 1U;
    k->last_action_completion = body[NUKI_KS_OFF_LAST_ACTION_COMPLETION];
  }
  if (len > NUKI_KS_OFF_DOOR_SENSOR) {
    k->has_door_sensor = 1U;
    k->door_sensor = body[NUKI_KS_OFF_DOOR_SENSOR];
  }
  if (len > NUKI_KS_OFF_NIGHT_MODE) {
    k->has_night_mode = 1U;
    k->night_mode = body[NUKI_KS_OFF_NIGHT_MODE] != 0U;
  }
  if (len > NUKI_KS_OFF_ACCESSORY_BATTERY) {
    const uint8_t a = body[NUKI_KS_OFF_ACCESSORY_BATTERY];
    k->has_accessory_battery = 1U;
    k->keypad_present = (a & NUKI_ACCESSORY_KEYPAD_SUPPORTED) != 0U;
    k->keypad_battery_critical = (a & NUKI_ACCESSORY_KEYPAD_CRITICAL) != 0U;
    k->door_sensor_battery_present =
        (a & NUKI_ACCESSORY_DOOR_SENSOR_SUPPORTED) != 0U;
    k->door_sensor_battery_critical =
        (a & NUKI_ACCESSORY_DOOR_SENSOR_CRITICAL) != 0U;
  }
  if (len > NUKI_KS_OFF_REMOTE_ACCESS) {
    k->has_remote_access = 1U;
    k->remote_access = body[NUKI_KS_OFF_REMOTE_ACCESS];
  }
  return NUKI_UART_OK;
}

/* ── Config 0x0015 (spec pp.42-46) ───────────────────────────────────── */

/*
 * The first 72 bytes (up to HomeKit status) exist on every generation; the
 * Timezone ID, Device Type, Capabilities, Has Keypad 2 and Matter status
 * were appended by later API versions (change log p.90) and are reported
 * through has_* flags.  Latitude / longitude are skipped (privacy, and the
 * host has no use for them).
 */
#define NUKI_CONFIG_BASE_LEN 72
#define NUKI_CONFIG_NAME_LEN 32

/* Config "Device Type" (p.45) */
#define NUKI_CONFIG_DEVICE_SL_1_2 0x00
#define NUKI_CONFIG_DEVICE_OPENER 0x02
#define NUKI_CONFIG_DEVICE_SMART_DOOR 0x03
#define NUKI_CONFIG_DEVICE_SL_3_4 0x04
#define NUKI_CONFIG_DEVICE_ULTRA 0x05

typedef struct {
  uint32_t nuki_id;
  char name[NUKI_CONFIG_NAME_LEN + 1];
  uint8_t auto_unlatch;
  uint8_t pairing_enabled;
  uint8_t button_enabled;
  uint8_t led_enabled;
  uint8_t led_brightness;
  nuki_ts_t time;
  int16_t tz_offset_min;
  uint8_t dst_mode;
  uint8_t has_fob;
  uint8_t fob_action[3];
  uint8_t single_lock;
  uint8_t advertising_mode;
  uint8_t has_keypad;
  uint8_t fw_version[3]; /* major.minor.patch */
  uint8_t hw_revision[2];
  uint8_t homekit_status;
  uint8_t has_timezone_id;
  uint16_t timezone_id;
  uint8_t has_device_type;
  uint8_t device_type; /* NUKI_CONFIG_DEVICE_* */
  uint8_t has_capabilities;
  uint8_t capabilities; /* bit0 WiFi, bit1 Thread/Matter */
  uint8_t has_keypad2_flag;
  uint8_t has_keypad2;
  uint8_t has_matter_status;
  uint8_t matter_status;
} nuki_config_t;

/* Input = UART DATA of a 0x86 frame: [15 00][Config payload]. */
static inline int nuki_uart_parse_config(const uint8_t *data, size_t len,
                                         nuki_config_t *c) {
  const uint8_t *p;
  size_t n;

  if (data == NULL || c == NULL) {
    return NUKI_UART_ERR_INVALID;
  }
  if (len < 2U || nuki_uart_get_u16(data) != NUKI_CMD_ID_CONFIG) {
    return NUKI_UART_ERR_INVALID;
  }
  p = data + 2;
  n = len - 2U;
  if (n < NUKI_CONFIG_BASE_LEN) {
    return NUKI_UART_ERR_SHORT;
  }
  memset(c, 0, sizeof(*c));
  c->nuki_id = nuki_uart_get_u32(p);
  nuki_uart_copy_name(c->name, sizeof(c->name), p + 4, NUKI_CONFIG_NAME_LEN);
  /* p + 36 .. 43: latitude, longitude (float) — not decoded */
  c->auto_unlatch = p[44];
  c->pairing_enabled = p[45];
  c->button_enabled = p[46];
  c->led_enabled = p[47];
  c->led_brightness = p[48];
  nuki_uart_get_ts(p + 49, &c->time);
  c->tz_offset_min = (int16_t)nuki_uart_get_u16(p + 56);
  c->dst_mode = p[58];
  c->has_fob = p[59];
  c->fob_action[0] = p[60];
  c->fob_action[1] = p[61];
  c->fob_action[2] = p[62];
  c->single_lock = p[63];
  c->advertising_mode = p[64];
  c->has_keypad = p[65];
  c->fw_version[0] = p[66];
  c->fw_version[1] = p[67];
  c->fw_version[2] = p[68];
  c->hw_revision[0] = p[69];
  c->hw_revision[1] = p[70];
  c->homekit_status = p[71];
  if (n >= 74U) {
    c->has_timezone_id = 1U;
    c->timezone_id = nuki_uart_get_u16(p + 72);
  }
  if (n >= 75U) {
    c->has_device_type = 1U;
    c->device_type = p[74];
  }
  if (n >= 76U) {
    c->has_capabilities = 1U;
    c->capabilities = p[75];
  }
  if (n >= 77U) {
    c->has_keypad2_flag = 1U;
    c->has_keypad2 = p[76];
  }
  if (n >= 78U) {
    c->has_matter_status = 1U;
    c->matter_status = p[77];
  }
  return NUKI_UART_OK;
}

/* A keypad is paired when either generation flag is set (Keypad 1 / 2.0). */
static inline int nuki_config_keypad_paired(const nuki_config_t *c) {
  return c->has_keypad != 0U || (c->has_keypad2_flag && c->has_keypad2 != 0U);
}

/*
 * Map the Config device type onto the bridge's PAIR/diagnostics device
 * type: 0x05 Ultra -> NUKI_UART_DEVICE_ULTRA, every other smart lock ->
 * CLASSIC (uint16 PIN), opener -> OPENER.  0 (AUTO) when unknown.
 */
static inline uint8_t nuki_config_uart_device_type(const nuki_config_t *c) {
  if (!c->has_device_type) {
    return NUKI_UART_DEVICE_AUTO;
  }
  switch (c->device_type) {
  case NUKI_CONFIG_DEVICE_ULTRA:
    return NUKI_UART_DEVICE_ULTRA;
  case NUKI_CONFIG_DEVICE_OPENER:
    return NUKI_UART_DEVICE_OPENER;
  case NUKI_CONFIG_DEVICE_SL_1_2:
  case NUKI_CONFIG_DEVICE_SMART_DOOR:
  case NUKI_CONFIG_DEVICE_SL_3_4:
    return NUKI_UART_DEVICE_CLASSIC;
  default:
    return NUKI_UART_DEVICE_AUTO;
  }
}

/* ── Name tables ─────────────────────────────────────────────────────── */
/*
 * Strings match the ones the NukiBleEsp32-based ESPHome component emits in
 * its Home Assistant events, so automations written for it keep working.
 */

static inline const char *nuki_lock_action_name(uint8_t action) {
  switch (action) {
  case 0x01:
    return "Unlock";
  case 0x02:
    return "Lock";
  case 0x03:
    return "Unlatch";
  case 0x04:
    return "LockNgo";
  case 0x05:
    return "LockNgoUnlatch";
  case 0x06:
    return "FullLock";
  case 0x50:
    return "FobNoAction";
  case 0x5A:
    return "ButtonNoAction";
  case 0x81:
    return "FobAction1";
  case 0x82:
    return "FobAction2";
  case 0x83:
    return "FobAction3";
  default:
    return "Unknown";
  }
}

static inline const char *nuki_trigger_name(uint8_t trigger) {
  switch (trigger) {
  case NUKI_TRIGGER_SYSTEM:
    return "system";
  case NUKI_TRIGGER_MANUAL:
    return "manual";
  case NUKI_TRIGGER_BUTTON:
    return "button";
  case NUKI_TRIGGER_AUTOMATIC:
    return "automatic";
  case NUKI_TRIGGER_AUTO_LOCK:
    return "autoLock";
  case 0xAB:
    return "homekit";
  case 0xAC:
    return "mqtt";
  default:
    return "undefined";
  }
}

static inline const char *nuki_completion_status_name(uint8_t status) {
  switch (status) {
  case 0x00:
    return "success";
  case 0x01:
    return "motorBlocked";
  case 0x02:
    return "canceled";
  case 0x03:
    return "tooRecent";
  case 0x04:
    return "busy";
  case 0x05:
    return "lowMotorVoltage";
  case 0x06:
    return "clutchFailure";
  case 0x07:
    return "motorPowerFailure";
  case 0x08:
    return "incompleteFailure";
  case 0x09:
    return "rejected";
  case 0x0A:
    return "rejectedNightmode";
  case 0x0B:
    return "failure";
  case NUKI_COMPLETION_INVALID_CODE:
    return "invalidCode";
  case NUKI_COMPLETION_INVALID_FINGERPRINT:
    return "invalidFingerprint";
  case 0xFE:
    return "otherError";
  case 0xFF:
    return "unknown";
  default:
    return "undefined";
  }
}

static inline const char *nuki_log_type_name(uint8_t type) {
  switch (type) {
  case NUKI_LOG_TYPE_LOGGING_ENABLED:
    return "LoggingEnabled";
  case NUKI_LOG_TYPE_LOCK_ACTION:
    return "LockAction";
  case NUKI_LOG_TYPE_CALIBRATION:
    return "Calibration";
  case NUKI_LOG_TYPE_INIT_RUN:
    return "InitializationRun";
  case NUKI_LOG_TYPE_KEYPAD_ACTION:
    return "KeypadAction";
  case NUKI_LOG_TYPE_DOOR_SENSOR:
    return "DoorSensor";
  case NUKI_LOG_TYPE_DOOR_SENSOR_LOGGING:
    return "DoorSensorLoggingEnabled";
  default:
    return "Unknown";
  }
}

/* Keypad action source (log type 0x05 byte 2) */
static inline const char *nuki_keypad_source_name(uint8_t source) {
  switch (source) {
  case 0x00:
    return "arrowkey";
  case 0x01:
    return "code";
  case 0x02:
    return "fingerprint";
  default:
    return "unknown";
  }
}

/* Door sensor log data (log type 0x06 byte 1) */
static inline const char *nuki_door_log_action_name(uint8_t v) {
  switch (v) {
  case 0x00:
    return "DoorOpened";
  case 0x01:
    return "DoorClosed";
  case 0x02:
    return "SensorJammed";
  case 0x03:
    return "SensorTampered";
  default:
    return "Unknown";
  }
}

/*
 * Keyturner States door sensor state (spec p.32).  The 1.0-2.0 and the
 * 3.0-Ultra value sets are disjoint, so one table names both without
 * knowing the generation; 0x00 means "not paired" on 3.0+.
 */
static inline const char *nuki_door_sensor_state_name(uint8_t state) {
  switch (state) {
  case NUKI_DOOR_UNAVAILABLE:
    return "unavailable";
  case NUKI_DOOR_DEACTIVATED:
    return "deactivated";
  case NUKI_DOOR_CLOSED:
    return "doorClosed";
  case NUKI_DOOR_OPENED:
    return "doorOpened";
  case NUKI_DOOR_STATE_UNKNOWN:
    return "doorStateUnknown";
  case NUKI_DOOR_CALIBRATING:
    return "calibrating";
  case NUKI_DOOR_UNCALIBRATED:
    return "uncalibrated";
  case NUKI_DOOR_TAMPERED:
    return "tampered";
  case NUKI_DOOR_UNKNOWN:
    return "unknown";
  default:
    return "undefined";
  }
}

/* Keyturner States byte 0 (p.30) */
static inline const char *nuki_nuki_state_name(uint8_t state) {
  switch (state) {
  case NUKI_NUKI_STATE_UNINITIALIZED:
    return "uninitialized";
  case NUKI_NUKI_STATE_PAIRING_MODE:
    return "pairingMode";
  case NUKI_NUKI_STATE_DOOR_MODE:
    return "doorMode";
  case NUKI_NUKI_STATE_MAINTENANCE_MODE:
    return "maintenanceMode";
  default:
    return "undefined";
  }
}

/* Config "Device Type" (p.45) */
static inline const char *nuki_config_device_type_name(uint8_t t) {
  switch (t) {
  case NUKI_CONFIG_DEVICE_SL_1_2:
    return "Smart Lock 1.0/2.0";
  case NUKI_CONFIG_DEVICE_OPENER:
    return "Opener";
  case NUKI_CONFIG_DEVICE_SMART_DOOR:
    return "Smart Door";
  case NUKI_CONFIG_DEVICE_SL_3_4:
    return "Smart Lock 3.0/4.0";
  case NUKI_CONFIG_DEVICE_ULTRA:
    return "Smart Lock Ultra";
  default:
    return "unknown";
  }
}

/* Config "Matter status" (p.46) */
static inline const char *nuki_matter_status_name(uint8_t s) {
  switch (s) {
  case 0x00:
    return "notAvailable";
  case 0x01:
    return "disabled";
  case 0x02:
    return "disabledLegacy";
  case 0x03:
    return "enabled";
  case 0x04:
    return "enabledPaired";
  default:
    return "undefined";
  }
}

/*
 * Composite door security state (same three values as hass_nuki_ng's
 * `door_security_state` sensor): locked + closed, closed but not locked,
 * everything else (open, unknown sensor, ...).
 */
static inline const char *nuki_door_security_state_name(uint8_t lock_state,
                                                        uint8_t door_state) {
  if (door_state == NUKI_DOOR_CLOSED) {
    return lock_state == NUKI_LOCK_STATE_LOCKED ? "closedAndLocked"
                                                : "closedAndUnlocked";
  }
  return "open";
}

#ifdef __cplusplus
} /* extern "C" */
#endif
