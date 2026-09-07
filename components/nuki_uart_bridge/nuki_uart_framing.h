#pragma once

/*
 * Host-side wire format of the nRF52840 Nuki bridge UART protocol.
 *
 * This header is intentionally free of ESPHome / ESP-IDF dependencies so the
 * exact same code can be compiled and unit-tested on a host compiler
 * (see tests/test_uart_framing.c).  Everything is `static inline` C99 that is
 * also valid C++.
 *
 * Wire format (both directions):
 *
 *   [0x00] <COBS-encoded frame> [0x00]
 *
 *   v1 frame: [TYPE:1] [DATA:0..N] [CRC16:2 LE]
 *   v2 frame: [TYPE:1] [SEQ:2 LE] [DATA:0..N] [CRC16:2 LE]
 *
 * CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no xorout)
 * computed over everything before the CRC bytes.  The bridge boots in v1 and
 * switches to v2 after HELLO; a host HELLO is always sent in v1 framing.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Protocol constants (mirror of src/nuki_command.h on the bridge) ─── */

#define NUKI_UART_PROTO_V1 0x01
#define NUKI_UART_PROTO_V2 0x02

/* Largest decoded frame the bridge accepts (MSG_BUF_SIZE): our TX limit. */
#define NUKI_UART_FRAME_MAX 128
/* COBS worst case for n bytes plus the two 0x00 delimiters. */
#define NUKI_UART_COBS_MAX(n) ((n) + ((n) / 254) + 1)
#define NUKI_UART_WIRE_MAX (NUKI_UART_COBS_MAX(NUKI_UART_FRAME_MAX) + 2)
/*
 * RX side is sized to the bridge's raw wire limit (RX_BUF_SIZE 256) rather
 * than MSG_BUF_SIZE so a larger diagnostics payload in a future bridge
 * build does not have to change the host parser.
 */
#define NUKI_UART_RX_WIRE_MAX 256
#define NUKI_UART_RX_FRAME_MAX 256

/* Commands (host -> bridge) */
#define NUKI_UART_CMD_UNLOCK 0x01
#define NUKI_UART_CMD_LOCK 0x02
#define NUKI_UART_CMD_UNLATCH 0x03
#define NUKI_UART_CMD_STATUS 0x04
#define NUKI_UART_CMD_PAIR 0x05
#define NUKI_UART_CMD_LOCK_N_GO 0x06
#define NUKI_UART_CMD_METRICS 0x07
#define NUKI_UART_CMD_LOCK_N_GO_UNLATCH 0x08
#define NUKI_UART_CMD_FULL_LOCK 0x09
#define NUKI_UART_CMD_FOB_1 0x0A
#define NUKI_UART_CMD_FOB_2 0x0B
#define NUKI_UART_CMD_FOB_3 0x0C
#define NUKI_UART_CMD_REQ_LOCK_STATE 0x10
#define NUKI_UART_CMD_REQ_BATTERY 0x11
/* Phase 4 passthroughs (docs/host-integration.md §7 on the bridge):
 * UPDATE_TIME  [year:2 LE][month][day][hour][min][sec] + PIN  (0x0021)
 * VERIFY_PIN   PIN only                                       (0x0020)
 * SET_ACTION_SUFFIX [suffix: 0..20 bytes], ACK only; the bridge appends it
 *   to every Lock Action it builds (spec p.36 "Name suffix"). */
#define NUKI_UART_CMD_UPDATE_TIME 0x13
#define NUKI_UART_CMD_VERIFY_PIN 0x14
#define NUKI_UART_CMD_SET_ACTION_SUFFIX 0x15
#define NUKI_UART_CMD_REQ_CONFIG 0x20
#define NUKI_UART_CMD_REQ_CALIBRATION 0x70
#define NUKI_UART_CMD_REQ_REBOOT 0x73
#define NUKI_UART_CMD_UNPAIR 0x74
#define NUKI_UART_CMD_SET_LINK_PROFILE 0x76
#define NUKI_UART_CMD_PING 0x7C
#define NUKI_UART_CMD_HELLO 0x7D
#define NUKI_UART_CMD_REQ_DIAGNOSTICS 0x7E

/* Responses (bridge -> host) */
#define NUKI_UART_RSP_ACK 0x80
#define NUKI_UART_RSP_STATUS 0x81
#define NUKI_UART_RSP_ERROR 0x82
#define NUKI_UART_RSP_PAIRING_COMPLETE 0x83
#define NUKI_UART_RSP_METRICS 0x84
#define NUKI_UART_RSP_STATE_CHANGE 0x85
#define NUKI_UART_RSP_CONFIG 0x86
#define NUKI_UART_RSP_BATTERY_REPORT 0x8B
#define NUKI_UART_RSP_DIAGNOSTICS 0x8C
#define NUKI_UART_RSP_CONN_STATUS 0x8D
#define NUKI_UART_RSP_ERROR_REPORT 0x8E
#define NUKI_UART_RSP_HELLO 0x90

/* Error codes carried by NUKI_UART_RSP_ERROR (docs/uart-error-codes.md) */
#define NUKI_UART_ERR_PAIRING_BUSY 0x01
#define NUKI_UART_ERR_NOT_PAIRED 0x02
#define NUKI_UART_ERR_PAIRING_FAILED 0x03
#define NUKI_UART_ERR_SCAN_START_FAILED 0x04
#define NUKI_UART_ERR_METRICS_UNAVAILABLE 0x05
#define NUKI_UART_ERR_PIN_REQUIRED 0x06
#define NUKI_UART_ERR_PAIRING_WINDOW_CLOSED 0x07
#define NUKI_UART_ERR_ENCRYPT_FAILED 0x10
#define NUKI_UART_ERR_DECRYPT_FAILED 0x11
#define NUKI_UART_ERR_TIMEOUT 0x20
#define NUKI_UART_ERR_QUEUE_FULL 0x21
#define NUKI_UART_ERR_NOT_CONNECTED 0x22
#define NUKI_UART_ERR_LOCK_BUSY 0x23
#define NUKI_UART_ERR_INVALID_PAYLOAD 0x30
#define NUKI_UART_ERR_UNSUPPORTED 0x31
#define NUKI_UART_ERR_ANTI_REPLAY 0x40
#define NUKI_UART_ERR_UART_AUTH 0x41
#define NUKI_UART_ERR_LOCK_ERROR 0x50
#define NUKI_UART_ERR_INTERNAL 0xFE
#define NUKI_UART_ERR_UNKNOWN_CMD 0xFF

/* CONN_STATUS (0x8D) event ids: [event:1][value:1] */
#define NUKI_UART_CONN_EVT_STATE 0x01
#define NUKI_UART_CONN_EVT_RSSI 0x02
#define NUKI_UART_CONN_EVT_HOST_LINK 0x03
#define NUKI_UART_CONN_EVT_BEACON 0x04

/* conn_mgr states reported by CONN_EVT_STATE */
#define NUKI_UART_CONN_IDLE 0
#define NUKI_UART_CONN_SCANNING 1
#define NUKI_UART_CONN_CONNECTING 2
#define NUKI_UART_CONN_DISCOVERING 3
#define NUKI_UART_CONN_SUBSCRIBING 4
#define NUKI_UART_CONN_CONNECTED 5
#define NUKI_UART_CONN_BACKOFF_WAIT 6

/* Nuki command ids that prefix forwarded lock messages ([cmd_id:2 LE][..]) */
#define NUKI_CMD_ID_KEYTURNER_STATES 0x000C
#define NUKI_CMD_ID_STATUS 0x000E
#define NUKI_CMD_ID_ERROR_REPORT 0x0012
#define NUKI_CMD_ID_CONFIG 0x0015

/* Nuki state = Keyturner States byte 0 (API v2.3.1 p.30) */
#define NUKI_NUKI_STATE_UNINITIALIZED 0x00
#define NUKI_NUKI_STATE_PAIRING_MODE 0x01
#define NUKI_NUKI_STATE_DOOR_MODE 0x02
#define NUKI_NUKI_STATE_MAINTENANCE_MODE 0x04

/* Keyturner States battery byte (p.32) and accessory battery byte (p.33) */
#define NUKI_BATTERY_CRITICAL_BIT 0x01
#define NUKI_BATTERY_CHARGING_BIT 0x02
#define NUKI_ACCESSORY_KEYPAD_SUPPORTED 0x01
#define NUKI_ACCESSORY_KEYPAD_CRITICAL 0x02
#define NUKI_ACCESSORY_DOOR_SENSOR_SUPPORTED 0x04
#define NUKI_ACCESSORY_DOOR_SENSOR_CRITICAL 0x08

/* Nuki lock error codes that concern the PIN (pp.76-77) */
#define NUKI_K_ERROR_BAD_PIN 0x21
#define NUKI_K_ERROR_TOO_MANY_PIN_ATTEMPTS 0x28

/* Nuki lock states (API v2.3.1 p.31) */
#define NUKI_LOCK_STATE_UNCALIBRATED 0x00
#define NUKI_LOCK_STATE_LOCKED 0x01
#define NUKI_LOCK_STATE_UNLOCKING 0x02
#define NUKI_LOCK_STATE_UNLOCKED 0x03
#define NUKI_LOCK_STATE_LOCKING 0x04
#define NUKI_LOCK_STATE_UNLATCHED 0x05
#define NUKI_LOCK_STATE_UNLOCKED_LNG 0x06
#define NUKI_LOCK_STATE_UNLATCHING 0x07
#define NUKI_LOCK_STATE_CALIBRATION 0xFC
#define NUKI_LOCK_STATE_BOOT_RUN 0xFD
#define NUKI_LOCK_STATE_MOTOR_BLOCKED 0xFE
#define NUKI_LOCK_STATE_UNDEFINED 0xFF

/* Nuki Status (0x000E) values (p.79) */
#define NUKI_STATUS_COMPLETE 0x00
#define NUKI_STATUS_ACCEPTED 0x01

/* PAIR payload fields */
#define NUKI_UART_DEVICE_AUTO 0x00
#define NUKI_UART_DEVICE_CLASSIC 0x01
#define NUKI_UART_DEVICE_ULTRA 0x02
#define NUKI_UART_DEVICE_OPENER 0x03
#define NUKI_UART_ID_TYPE_APP 0x00
#define NUKI_UART_ID_TYPE_BRIDGE 0x01

/* SET_LINK_PROFILE payload */
#define NUKI_UART_LINK_PROFILE_ECO 0x00
#define NUKI_UART_LINK_PROFILE_ARMED 0x01

/* Return codes */
#define NUKI_UART_OK 0
#define NUKI_UART_ERR_INVALID (-1)
#define NUKI_UART_ERR_OVERFLOW (-2)
#define NUKI_UART_ERR_MALFORMED (-3)
#define NUKI_UART_ERR_CRC (-4)
#define NUKI_UART_ERR_SHORT (-5)

/* ── CRC-16/CCITT-FALSE ──────────────────────────────────────────────── */

static inline uint16_t nuki_uart_crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFFU;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)((uint16_t)data[i] << 8);
    for (int b = 0; b < 8; b++) {
      if (crc & 0x8000U) {
        crc = (uint16_t)((crc << 1) ^ 0x1021U);
      } else {
        crc = (uint16_t)(crc << 1);
      }
    }
  }
  return crc;
}

/* ── COBS ────────────────────────────────────────────────────────────── */

/* Returns the encoded length, or NUKI_UART_ERR_*. Output has no 0x00. */
static inline int nuki_uart_cobs_encode(uint8_t *dst, size_t dst_max,
                                        const uint8_t *src, size_t src_len) {
  size_t di = 0;
  size_t code_i;
  uint8_t code = 1;

  if (dst == NULL || (src == NULL && src_len > 0U) || src_len > 0x7FFFU) {
    return NUKI_UART_ERR_INVALID;
  }
  if (di >= dst_max) {
    return NUKI_UART_ERR_OVERFLOW;
  }
  code_i = di++;

  for (size_t si = 0; si < src_len; si++) {
    uint8_t b = src[si];
    if (b == 0x00) {
      dst[code_i] = code;
      code = 1;
      if (di >= dst_max) {
        return NUKI_UART_ERR_OVERFLOW;
      }
      code_i = di++;
      continue;
    }
    if (di >= dst_max) {
      return NUKI_UART_ERR_OVERFLOW;
    }
    dst[di++] = b;
    code++;
    if (code == 0xFF) {
      dst[code_i] = code;
      code = 1;
      if (di >= dst_max) {
        return NUKI_UART_ERR_OVERFLOW;
      }
      code_i = di++;
    }
  }
  dst[code_i] = code;
  return (int)di;
}

/* Returns the decoded length, or NUKI_UART_ERR_*. src must not contain 0x00. */
static inline int nuki_uart_cobs_decode(uint8_t *dst, size_t dst_max,
                                        const uint8_t *src, size_t src_len) {
  size_t di = 0;
  size_t si = 0;

  if ((dst == NULL && dst_max > 0U) || (src == NULL && src_len > 0U) ||
      src_len > 0x7FFFU) {
    return NUKI_UART_ERR_INVALID;
  }

  while (si < src_len) {
    uint8_t code = src[si++];
    if (code == 0x00) {
      return NUKI_UART_ERR_MALFORMED;
    }
    if ((size_t)(code - 1U) > src_len - si) {
      return NUKI_UART_ERR_MALFORMED;
    }
    for (uint8_t i = 1; i < code; i++) {
      if (di >= dst_max) {
        return NUKI_UART_ERR_OVERFLOW;
      }
      dst[di++] = src[si++];
    }
    if (code < 0xFF && si < src_len) {
      if (di >= dst_max) {
        return NUKI_UART_ERR_OVERFLOW;
      }
      dst[di++] = 0x00;
    }
  }
  return (int)di;
}

/* ── Frame build / parse ─────────────────────────────────────────────── */

typedef struct {
  uint8_t type;
  uint16_t seq;        /* 0 in v1 framing */
  const uint8_t *data; /* points into the caller's decoded buffer */
  size_t len;
} nuki_uart_msg_t;

/*
 * Build a complete wire frame: leading 0x00, COBS([TYPE][SEQ?][DATA][CRC]),
 * trailing 0x00.  `version` selects v1 (no SEQ) or v2.  Returns the number of
 * bytes written to `out`, or NUKI_UART_ERR_*.
 */
static inline int nuki_uart_build_frame(uint8_t *out, size_t out_max,
                                        uint8_t version, uint8_t type,
                                        uint16_t seq, const uint8_t *data,
                                        size_t len) {
  uint8_t frame[NUKI_UART_FRAME_MAX];
  size_t header = (version == NUKI_UART_PROTO_V2) ? 3U : 1U;
  size_t flen = header + len + 2U;
  uint16_t crc;
  int cobs_len;

  if (out == NULL || (data == NULL && len > 0U)) {
    return NUKI_UART_ERR_INVALID;
  }
  if (flen > sizeof(frame)) {
    return NUKI_UART_ERR_OVERFLOW;
  }

  frame[0] = type;
  if (version == NUKI_UART_PROTO_V2) {
    frame[1] = (uint8_t)seq;
    frame[2] = (uint8_t)(seq >> 8);
  }
  if (len > 0U) {
    memcpy(frame + header, data, len);
  }
  crc = nuki_uart_crc16(frame, header + len);
  frame[header + len] = (uint8_t)crc;
  frame[header + len + 1U] = (uint8_t)(crc >> 8);

  if (out_max < 2U) {
    return NUKI_UART_ERR_OVERFLOW;
  }
  out[0] = 0x00;
  cobs_len = nuki_uart_cobs_encode(out + 1, out_max - 2U, frame, flen);
  if (cobs_len < 0) {
    return cobs_len;
  }
  out[1 + (size_t)cobs_len] = 0x00;
  return cobs_len + 2;
}

/* The host HELLO is always v1: [0x7D][host_version][CRC16]. */
static inline int nuki_uart_build_hello(uint8_t *out, size_t out_max,
                                        uint8_t host_version) {
  return nuki_uart_build_frame(out, out_max, NUKI_UART_PROTO_V1,
                               NUKI_UART_CMD_HELLO, 0U, &host_version, 1U);
}

/*
 * Parse an already COBS-decoded frame.  Verifies the CRC and splits the
 * header according to `version`.  Returns NUKI_UART_OK or NUKI_UART_ERR_*.
 *
 * HELLO responses (0x90) are special: the bridge frames them in whatever
 * version was in effect when the HELLO arrived, and the boot-time HELLO is
 * always v1.  Their DATA is always 4 bytes, so the layout is derived from
 * the frame length (7 = v1, 9 = v2) instead of `version`.
 */
static inline int nuki_uart_parse_frame(const uint8_t *frame, size_t len,
                                        uint8_t version, nuki_uart_msg_t *msg) {
  size_t header;
  uint16_t crc_recv;

  if (frame == NULL || msg == NULL) {
    return NUKI_UART_ERR_INVALID;
  }
  if (len < 3U) {
    return NUKI_UART_ERR_SHORT;
  }
  crc_recv =
      (uint16_t)frame[len - 2] | (uint16_t)((uint16_t)frame[len - 1] << 8);
  if (crc_recv != nuki_uart_crc16(frame, len - 2U)) {
    return NUKI_UART_ERR_CRC;
  }

  if (frame[0] == NUKI_UART_RSP_HELLO) {
    if (len == 7U) {
      header = 1U;
    } else if (len == 9U) {
      header = 3U;
    } else {
      return NUKI_UART_ERR_MALFORMED;
    }
  } else {
    header = (version == NUKI_UART_PROTO_V2) ? 3U : 1U;
  }
  if (len < header + 2U) {
    return NUKI_UART_ERR_SHORT;
  }

  msg->type = frame[0];
  msg->seq = (header == 3U)
                 ? (uint16_t)((uint16_t)frame[1] | ((uint16_t)frame[2] << 8))
                 : 0U;
  msg->data = frame + header;
  msg->len = len - header - 2U;
  return NUKI_UART_OK;
}

/* ── Payload helpers ─────────────────────────────────────────────────── */

static inline uint16_t nuki_uart_get_u16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t nuki_uart_get_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static inline void nuki_uart_put_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

/*
 * Locate the Keyturner States body inside a STATE_CHANGE / STATUS payload.
 * The bridge forwards decrypted lock messages as [cmd_id:2 LE][payload]; a
 * bare payload (no cmd id) is also tolerated because a Nuki State byte can
 * never be 0x0C, 0x0E, 0x12 or 0x15.  Returns the body length (>= 2) or 0
 * if not a Keyturner States message.  *nuki_state / *lock_state receive bytes 0
 * and 1 (p.30).
 */
static inline size_t nuki_uart_keyturner_body(const uint8_t *data, size_t len,
                                              const uint8_t **body) {
  if (data == NULL || len < 2U) {
    return 0U;
  }
  if (len >= 4U && nuki_uart_get_u16(data) == NUKI_CMD_ID_KEYTURNER_STATES) {
    *body = data + 2;
    return len - 2U;
  }
  if (nuki_uart_get_u16(data) == NUKI_CMD_ID_STATUS ||
      nuki_uart_get_u16(data) == NUKI_CMD_ID_ERROR_REPORT ||
      nuki_uart_get_u16(data) == NUKI_CMD_ID_CONFIG) {
    return 0U;
  }
  *body = data;
  return len;
}

/*
 * PAIR payload: [device_type:1][pin:4 LE][id_type:1][app_id:4 LE].
 * Any prefix is accepted by the bridge, so trailing default fields are
 * omitted.  Returns the payload length (0..10).
 */
static inline size_t nuki_uart_build_pair_payload(uint8_t *out,
                                                  uint8_t device_type,
                                                  uint32_t pin, uint8_t id_type,
                                                  uint32_t app_id) {
  size_t n = 0;
  out[n++] = device_type;
  nuki_uart_put_u32(out + n, pin);
  n += 4;
  out[n++] = id_type;
  nuki_uart_put_u32(out + n, app_id);
  n += 4;

  /* trim trailing defaults */
  if (app_id == 0U) {
    n = 6;
    if (id_type == NUKI_UART_ID_TYPE_APP) {
      n = 5;
      if (pin == 0U) {
        n = 1;
        if (device_type == NUKI_UART_DEVICE_AUTO) {
          n = 0;
        }
      }
    }
  }
  return n;
}

#ifdef __cplusplus
} /* extern "C" */
#endif
