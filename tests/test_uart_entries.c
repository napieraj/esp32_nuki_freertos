/*
 * Host unit test for components/nuki_uart_bridge/nuki_uart_entries.h:
 * payload builders (PIN placement and width) and the length-driven parsers
 * for the 0x87 / 0x88 / 0x89 entry streams.
 *
 * Nuki publishes no vectors for these commands; the frames below are built
 * by hand from the field tables of API v2.3.1 (pp.22-25, 49-53, 63-73).
 *
 * Build & run:  make test-host
 */

#include "nuki_uart_entries.h"

#include <stdio.h>
#include <stdlib.h>

static int failures;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      failures++;                                                              \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
    }                                                                          \
  } while (0)

static void hexdump(const char *label, const uint8_t *p, size_t n) {
  printf("%-34s", label);
  for (size_t i = 0; i < n; i++) {
    printf("%02X%s", p[i], i + 1 < n ? " " : "");
  }
  printf("\n");
}

static int bytes_eq(const uint8_t *a, const uint8_t *b, size_t n) {
  return memcmp(a, b, n) == 0;
}

/* Append a NUL-padded name of `width` bytes. */
static size_t put_name(uint8_t *p, const char *s, size_t width) {
  memset(p, 0, width);
  memcpy(p, s, strlen(s));
  return width;
}

static size_t put_ts(uint8_t *p, uint16_t y, uint8_t mo, uint8_t d, uint8_t h,
                     uint8_t mi, uint8_t s) {
  p[0] = (uint8_t)y;
  p[1] = (uint8_t)(y >> 8);
  p[2] = mo;
  p[3] = d;
  p[4] = h;
  p[5] = mi;
  p[6] = s;
  return 7;
}

/* ── PIN width ───────────────────────────────────────────────────────── */

static void test_pin(void) {
  uint8_t b[4] = {0xAA, 0xAA, 0xAA, 0xAA};
  CHECK(nuki_uart_pin_width(NUKI_UART_DEVICE_CLASSIC) == 2);
  CHECK(nuki_uart_pin_width(NUKI_UART_DEVICE_ULTRA) == 4);
  CHECK(nuki_uart_pin_width(NUKI_UART_DEVICE_AUTO) == 2);
  /* gen 1-4: 1234 = 0x04D2 LE (host-integration.md §7 example) */
  CHECK(nuki_uart_put_pin(b, 1234, NUKI_UART_DEVICE_CLASSIC) == 2);
  CHECK(b[0] == 0xD2 && b[1] == 0x04 && b[2] == 0xAA);
  /* Ultra: 123456 = 0x0001E240 LE */
  CHECK(nuki_uart_put_pin(b, 123456, NUKI_UART_DEVICE_ULTRA) == 4);
  CHECK(b[0] == 0x40 && b[1] == 0xE2 && b[2] == 0x01 && b[3] == 0x00);
}

/* ── Builders ────────────────────────────────────────────────────────── */

static void test_builders(void) {
  uint8_t out[NUKI_UART_PAYLOAD_MAX];
  uint8_t wire[NUKI_UART_WIRE_MAX];

  /* Request Log Entries: newest 5, descending, no count, gen 1-4 PIN 1234 */
  {
    static const uint8_t exp[] = {0x00, 0x00, 0x00, 0x00, 0x05, 0x00,
                                  0x01, 0x00, 0xD2, 0x04};
    size_t n = nuki_uart_build_req_log_entries(
        out, 0, 5, NUKI_LOG_SORT_DESCENDING, 0, 1234, NUKI_UART_DEVICE_CLASSIC);
    CHECK(n == sizeof(exp));
    CHECK(bytes_eq(out, exp, sizeof(exp)));
    hexdump("REQ_LOG_ENTRIES payload (gen1-4)", out, n);
  }
  /* Same on Ultra: 4-byte PIN */
  {
    size_t n = nuki_uart_build_req_log_entries(
        out, 42, 5, NUKI_LOG_SORT_DESCENDING, 0, 65432, NUKI_UART_DEVICE_ULTRA);
    CHECK(n == 12);
    CHECK(out[0] == 42 && out[8] == 0x98 && out[9] == 0xFF && out[10] == 0 &&
          out[11] == 0);
  }
  /* Request Keypad Codes: offset 0, count 0xFFFF, Ultra PIN 065432 */
  {
    static const uint8_t exp[] = {0x00, 0x00, 0xFF, 0xFF,
                                  0x98, 0xFF, 0x00, 0x00};
    size_t n = nuki_uart_build_req_keypad_codes(out, 0, 0xFFFF, 65432,
                                                NUKI_UART_DEVICE_ULTRA);
    CHECK(n == sizeof(exp));
    CHECK(bytes_eq(out, exp, sizeof(exp)));
  }
  /* Add Keypad Code "Guest" 123456, no time limit, Ultra PIN 065432 */
  {
    uint8_t exp[48];
    size_t o = 0;
    exp[o++] = 0x40;
    exp[o++] = 0xE2;
    exp[o++] = 0x01;
    exp[o++] = 0x00; /* code 123456 */
    o += put_name(exp + o, "Guest", 20);
    exp[o++] = 0x00;           /* time limited */
    memset(exp + o, 0, 19);    /* from/until/weekdays/from time/until time */
    o += 19;
    exp[o++] = 0x98;
    exp[o++] = 0xFF;
    exp[o++] = 0x00;
    exp[o++] = 0x00; /* PIN 65432 */
    CHECK(o == 48);
    size_t n = nuki_uart_build_add_keypad(out, 123456, "Guest", NULL, 65432,
                                          NUKI_UART_DEVICE_ULTRA);
    CHECK(n == 48);
    CHECK(bytes_eq(out, exp, 48));
    hexdump("ADD_KEYPAD payload (Ultra)", out, n);
    /* full UART frame, v2, SEQ 7 */
    int w = nuki_uart_build_frame(wire, sizeof(wire), NUKI_UART_PROTO_V2,
                                  NUKI_UART_CMD_ADD_KEYPAD, 7, out, n);
    CHECK(w > 0);
    if (w > 0) {
      hexdump("ADD_KEYPAD wire (v2 seq 7)", wire, (size_t)w);
      /* decode back and check the body */
      uint8_t frame[NUKI_UART_RX_FRAME_MAX];
      nuki_uart_msg_t msg;
      int d = nuki_uart_cobs_decode(frame, sizeof(frame), wire + 1,
                                    (size_t)w - 2);
      CHECK(d == (int)(3 + n + 2));
      CHECK(nuki_uart_parse_frame(frame, (size_t)d, NUKI_UART_PROTO_V2,
                                  &msg) == NUKI_UART_OK);
      CHECK(msg.type == NUKI_UART_CMD_ADD_KEYPAD && msg.seq == 7 &&
            msg.len == n);
      CHECK(bytes_eq(msg.data, exp, n));
    }
    /* gen 1-4 variant is 2 bytes shorter and ends with the uint16 PIN */
    n = nuki_uart_build_add_keypad(out, 123456, "Guest", NULL, 1234,
                                   NUKI_UART_DEVICE_CLASSIC);
    CHECK(n == 46);
    CHECK(out[44] == 0xD2 && out[45] == 0x04);
    /* name longer than 20 chars is truncated, never overruns */
    n = nuki_uart_build_add_keypad(out, 123456, "abcdefghijklmnopqrstuvwxyz",
                                   NULL, 1234, NUKI_UART_DEVICE_CLASSIC);
    CHECK(n == 46 && out[4] == 'a' && out[23] == 't' && out[24] == 0x00);
  }
  /* Add Keypad Code with a time restriction */
  {
    nuki_keypad_limits_t lim;
    memset(&lim, 0, sizeof(lim));
    lim.allowed_from.year = 2026;
    lim.allowed_from.month = 9;
    lim.allowed_from.day = 7;
    lim.allowed_until.year = 2026;
    lim.allowed_until.month = 12;
    lim.allowed_until.day = 31;
    lim.weekdays = 0x7C; /* MO..FR */
    lim.from_hour = 8;
    lim.until_hour = 18;
    lim.until_minute = 30;
    size_t n = nuki_uart_build_add_keypad(out, 234567, "Cleaner", &lim, 1234,
                                          NUKI_UART_DEVICE_CLASSIC);
    CHECK(n == 46);
    CHECK(out[24] == 0x01);                 /* time limited */
    CHECK(out[25] == 0xEA && out[26] == 0x07); /* 2026 LE */
    CHECK(out[27] == 9 && out[28] == 7);
    CHECK(out[32] == 0xEA && out[34] == 12 && out[35] == 31);
    CHECK(out[39] == 0x7C && out[40] == 8 && out[41] == 0 && out[42] == 18 &&
          out[43] == 30);
  }
  /* Update Keypad Code id 3 -> "Guest2" 345678 disabled, Ultra */
  {
    size_t n = nuki_uart_build_update_keypad(out, 3, 345678, "Guest2", 0, NULL,
                                             65432, NUKI_UART_DEVICE_ULTRA);
    CHECK(n == 51);
    CHECK(out[0] == 0x03 && out[1] == 0x00);
    CHECK(nuki_uart_get_u32(out + 2) == 345678);
    CHECK(out[6] == 'G' && out[11] == '2' && out[12] == 0);
    CHECK(out[26] == 0x00 && out[27] == 0x00); /* enabled, time limited */
    CHECK(nuki_uart_get_u32(out + 47) == 65432);
  }
  /* Remove Keypad Code id 7, gen 1-4 */
  {
    static const uint8_t exp[] = {0x07, 0x00, 0xD2, 0x04};
    size_t n = nuki_uart_build_remove_keypad(out, 7, 1234,
                                             NUKI_UART_DEVICE_CLASSIC);
    CHECK(n == 4 && bytes_eq(out, exp, 4));
  }
  /* Request Authorization Entries: offset 0, count 32, Ultra */
  {
    size_t n = nuki_uart_build_req_auth_entries(out, 0, 32, 65432,
                                                NUKI_UART_DEVICE_ULTRA);
    CHECK(n == 8 && out[2] == 32 && out[3] == 0 && out[4] == 0x98);
  }
  /* SET_STATE_POLL 60 s */
  {
    size_t n = nuki_uart_build_set_state_poll(out, 60);
    CHECK(n == 2 && out[0] == 0x3C && out[1] == 0x00);
  }
  /* Every builder stays inside the bridge's 96-byte payload limit. */
  CHECK(47 + 4 <= NUKI_UART_PAYLOAD_MAX);
}

/* ── Log Entry 0x0032 ────────────────────────────────────────────────── */

static size_t build_log_entry(uint8_t *p, uint32_t index, uint32_t auth_id,
                              const char *name, uint8_t type,
                              const uint8_t *data, size_t data_len) {
  size_t o = 0;
  p[o++] = 0x32;
  p[o++] = 0x00;
  nuki_uart_put_u32(p + o, index);
  o += 4;
  o += put_ts(p + o, 2026, 9, 7, 12, 34, 56);
  nuki_uart_put_u32(p + o, auth_id);
  o += 4;
  o += put_name(p + o, name, 32);
  p[o++] = type;
  memcpy(p + o, data, data_len);
  o += data_len;
  return o;
}

static void test_log_entry(void) {
  uint8_t f[80];
  nuki_log_entry_t e;

  /* Type 0x02 lock action: unlock, trigger system, flags 0, success */
  {
    static const uint8_t d[] = {0x01, 0x00, 0x00, 0x00};
    size_t n = build_log_entry(f, 42, 34808, "Front Door App", 0x02, d, 4);
    CHECK(n == 54);
    CHECK(nuki_uart_parse_log_entry(f, n, &e) == NUKI_UART_OK);
    CHECK(e.index == 42);
    CHECK(e.ts.year == 2026 && e.ts.month == 9 && e.ts.day == 7);
    CHECK(e.ts.hour == 12 && e.ts.minute == 34 && e.ts.second == 56);
    CHECK(e.auth_id == 34808);
    CHECK(strcmp(e.name, "Front Door App") == 0);
    CHECK(e.type == NUKI_LOG_TYPE_LOCK_ACTION);
    CHECK(e.data_len == 4);
    CHECK(e.data[0] == 0x01 && e.data[1] == 0x00 && e.data[3] == 0x00);
    CHECK(strcmp(nuki_lock_action_name(e.data[0]), "Unlock") == 0);
    CHECK(strcmp(nuki_trigger_name(e.data[1]), "system") == 0);
    CHECK(strcmp(nuki_completion_status_name(e.data[3]), "success") == 0);
    CHECK(nuki_log_entry_code_id(&e) == 0);
  }
  /* Type 0x05 keypad action: unlock via code, success, code id 7 */
  {
    static const uint8_t d[] = {0x01, 0x01, 0x00, 0x07, 0x00};
    size_t n = build_log_entry(f, 43, 3, "Keypad", 0x05, d, 5);
    CHECK(n == 55);
    CHECK(nuki_uart_parse_log_entry(f, n, &e) == NUKI_UART_OK);
    CHECK(e.type == NUKI_LOG_TYPE_KEYPAD_ACTION && e.data_len == 5);
    CHECK(strcmp(nuki_keypad_source_name(e.data[1]), "code") == 0);
    CHECK(nuki_log_entry_code_id(&e) == 7);
  }
  /* Type 0x05 with an invalid code: completion 0xE0 */
  {
    static const uint8_t d[] = {0x01, 0x01, 0xE0, 0x00, 0x00};
    size_t n = build_log_entry(f, 44, 3, "Keypad", 0x05, d, 5);
    CHECK(nuki_uart_parse_log_entry(f, n, &e) == NUKI_UART_OK);
    CHECK(e.data[2] == NUKI_COMPLETION_INVALID_CODE);
    CHECK(strcmp(nuki_completion_status_name(e.data[2]), "invalidCode") == 0);
  }
  /* Type 0x06 door sensor: 1 byte, empty name */
  {
    static const uint8_t d[] = {0x00};
    size_t n = build_log_entry(f, 45, 0, "", 0x06, d, 1);
    CHECK(n == 51);
    CHECK(nuki_uart_parse_log_entry(f, n, &e) == NUKI_UART_OK);
    CHECK(e.type == NUKI_LOG_TYPE_DOOR_SENSOR && e.data_len == 1);
    CHECK(e.name[0] == '\0');
    CHECK(strcmp(nuki_door_log_action_name(e.data[0]), "DoorOpened") == 0);
    CHECK(strcmp(nuki_log_type_name(e.type), "DoorSensor") == 0);
  }
  /* Type 0x07 door sensor logging toggled */
  {
    static const uint8_t d[] = {0x01};
    size_t n = build_log_entry(f, 46, 0, "", 0x07, d, 1);
    CHECK(nuki_uart_parse_log_entry(f, n, &e) == NUKI_UART_OK);
    CHECK(e.type == NUKI_LOG_TYPE_DOOR_SENSOR_LOGGING && e.data[0] == 1);
  }
  /* Unknown type with 8 data bytes: capped at NUKI_LOG_DATA_MAX, no error */
  {
    static const uint8_t d[] = {1, 2, 3, 4, 5, 6, 7, 8};
    size_t n = build_log_entry(f, 47, 0, "x", 0x42, d, 8);
    CHECK(nuki_uart_parse_log_entry(f, n, &e) == NUKI_UART_OK);
    CHECK(e.data_len == NUKI_LOG_DATA_MAX && e.data[4] == 5);
  }
  /* Type 0x02 truncated to 2 data bytes: parses, data_len says so */
  {
    static const uint8_t d[] = {0x02, 0x01};
    size_t n = build_log_entry(f, 48, 0, "x", 0x02, d, 2);
    CHECK(nuki_uart_parse_log_entry(f, n, &e) == NUKI_UART_OK);
    CHECK(e.data_len == 2);
  }
  /* 32-byte name without NUL is terminated by the parser */
  {
    static const uint8_t d[] = {0x01, 0x00, 0x00, 0x00};
    size_t n = build_log_entry(f, 49, 1, "", 0x02, d, 4);
    memset(f + 2 + 4 + 7 + 4, 'N', 32);
    CHECK(nuki_uart_parse_log_entry(f, n, &e) == NUKI_UART_OK);
    CHECK(strlen(e.name) == 32);
  }
  /* Errors: short, wrong id */
  {
    static const uint8_t d[] = {0x01, 0x00, 0x00, 0x00};
    size_t n = build_log_entry(f, 50, 1, "x", 0x02, d, 4);
    CHECK(nuki_uart_parse_log_entry(f, 49, &e) == NUKI_UART_ERR_SHORT);
    CHECK(nuki_uart_parse_log_entry(f, 50, &e) == NUKI_UART_OK); /* no data */
    f[0] = 0x33;
    CHECK(nuki_uart_parse_log_entry(f, n, &e) == NUKI_UART_ERR_INVALID);
    CHECK(nuki_uart_parse_log_entry(NULL, n, &e) == NUKI_UART_ERR_INVALID);
    CHECK(nuki_uart_parse_log_entry(f, 1, &e) == NUKI_UART_ERR_INVALID);
  }
  /* Log Entry Count 0x0033 */
  {
    static const uint8_t c[] = {0x33, 0x00, 0x01, 0x7B, 0x00, 0x01, 0x00};
    nuki_log_count_t lc;
    CHECK(nuki_uart_parse_log_count(c, sizeof(c), &lc) == NUKI_UART_OK);
    CHECK(lc.logging_enabled == 1 && lc.count == 123 &&
          lc.door_sensor_enabled == 1 && lc.door_sensor_logging_enabled == 0);
    CHECK(nuki_uart_parse_log_count(c, 4, &lc) == NUKI_UART_ERR_SHORT);
  }
  CHECK(nuki_log_data_len(0x02) == 4 && nuki_log_data_len(0x05) == 5 &&
        nuki_log_data_len(0x06) == 1 && nuki_log_data_len(0x99) == 0);
}

/* ── Keypad Code 0x0045 (variable length) ────────────────────────────── */

static size_t build_keypad_code(uint8_t *p, uint16_t id, uint32_t code,
                                const char *name, uint8_t enabled,
                                uint16_t lock_count, int time_limited) {
  size_t o = 0;
  p[o++] = 0x45;
  p[o++] = 0x00;
  nuki_uart_put_u16(p + o, id);
  o += 2;
  nuki_uart_put_u32(p + o, code);
  o += 4;
  o += put_name(p + o, name, 20);
  p[o++] = enabled;
  o += put_ts(p + o, 2025, 1, 2, 3, 4, 5);
  o += put_ts(p + o, 2026, 9, 6, 20, 15, 0);
  nuki_uart_put_u16(p + o, lock_count);
  o += 2;
  p[o++] = time_limited ? 1 : 0;
  if (time_limited) {
    o += put_ts(p + o, 2026, 9, 1, 0, 0, 0);
    o += put_ts(p + o, 2026, 9, 30, 23, 59, 59);
    p[o++] = 0x7C;
    p[o++] = 8;
    p[o++] = 0;
    p[o++] = 18;
    p[o++] = 30;
  }
  return o;
}

static void test_keypad_code(void) {
  uint8_t f[80];
  nuki_keypad_code_t k;

  /* not time limited: exactly 46 bytes */
  {
    size_t n = build_keypad_code(f, 7, 123456, "Guest", 1, 12, 0);
    CHECK(n == NUKI_KEYPAD_CODE_BASE_LEN && n == 46);
    CHECK(nuki_uart_parse_keypad_code(f, n, &k) == NUKI_UART_OK);
    CHECK(k.code_id == 7 && k.code == 123456);
    CHECK(strcmp(k.name, "Guest") == 0 && k.enabled == 1);
    CHECK(k.created.year == 2025 && k.created.month == 1 && k.created.day == 2);
    CHECK(k.last_active.year == 2026 && k.last_active.hour == 20 &&
          k.last_active.minute == 15);
    CHECK(k.lock_count == 12 && k.time_limited == 0);
    /* truncated base is rejected */
    CHECK(nuki_uart_parse_keypad_code(f, n - 1, &k) == NUKI_UART_ERR_SHORT);
  }
  /* time limited: 65 bytes with the tail */
  {
    size_t n = build_keypad_code(f, 8, 234567, "Cleaner", 0, 0, 1);
    CHECK(n == 65);
    CHECK(nuki_uart_parse_keypad_code(f, n, &k) == NUKI_UART_OK);
    CHECK(k.code_id == 8 && k.enabled == 0 && k.time_limited == 1);
    CHECK(k.limits.allowed_from.year == 2026 && k.limits.allowed_from.day == 1);
    CHECK(k.limits.allowed_until.day == 30 && k.limits.allowed_until.second == 59);
    CHECK(k.limits.weekdays == 0x7C && k.limits.from_hour == 8 &&
          k.limits.until_hour == 18 && k.limits.until_minute == 30);
    /* flag set but tail missing -> short */
    CHECK(nuki_uart_parse_keypad_code(f, 46, &k) == NUKI_UART_ERR_SHORT);
    CHECK(nuki_uart_parse_keypad_code(f, 64, &k) == NUKI_UART_ERR_SHORT);
  }
  /* wrong command id */
  {
    size_t n = build_keypad_code(f, 9, 111111, "x", 1, 0, 0);
    f[0] = 0x44;
    CHECK(nuki_uart_parse_keypad_code(f, n, &k) == NUKI_UART_ERR_INVALID);
  }
  /* Keypad Code Count 0x0044 and Keypad Code ID 0x0042 */
  {
    static const uint8_t c[] = {0x44, 0x00, 0x03, 0x00};
    uint16_t v = 0;
    CHECK(nuki_uart_parse_keypad_count(c, sizeof(c), &v) == NUKI_UART_OK);
    CHECK(v == 3);
    CHECK(nuki_uart_parse_keypad_count(c, 3, &v) == NUKI_UART_ERR_SHORT);
  }
  {
    uint8_t id[11] = {0x42, 0x00, 0x0B, 0x00};
    uint16_t v = 0;
    nuki_ts_t ts;
    put_ts(id + 4, 2026, 9, 7, 1, 2, 3);
    CHECK(nuki_uart_parse_keypad_code_id(id, sizeof(id), &v, &ts) ==
          NUKI_UART_OK);
    CHECK(v == 11 && ts.year == 2026 && ts.second == 3);
    /* id without the date is still accepted */
    CHECK(nuki_uart_parse_keypad_code_id(id, 4, &v, &ts) == NUKI_UART_OK);
    CHECK(ts.year == 0);
  }
}

/* ── Authorization Entry 0x000A ──────────────────────────────────────── */

static void test_auth_entry(void) {
  uint8_t f[80];
  nuki_auth_entry_t a;
  size_t o = 0;

  f[o++] = 0x0A;
  f[o++] = 0x00;
  nuki_uart_put_u32(f + o, 34808);
  o += 4;
  f[o++] = 0x00; /* App */
  o += put_name(f + o, "ESP32 bridge", 32);
  f[o++] = 1; /* enabled */
  f[o++] = 0; /* remote allowed */
  CHECK(o == NUKI_AUTH_ENTRY_MIN_LEN && o == 41);
  /* minimal frame (no dates) */
  CHECK(nuki_uart_parse_auth_entry(f, o, &a) == NUKI_UART_OK);
  CHECK(a.auth_id == 34808 && a.id_type == 0 && a.enabled == 1);
  CHECK(strcmp(a.name, "ESP32 bridge") == 0 && a.has_details == 0);
  CHECK(nuki_uart_parse_auth_entry(f, o - 1, &a) == NUKI_UART_ERR_SHORT);
  /* full frame with details */
  o += put_ts(f + o, 2024, 5, 6, 7, 8, 9);
  o += put_ts(f + o, 2026, 9, 7, 10, 11, 12);
  nuki_uart_put_u16(f + o, 321);
  o += 2;
  f[o++] = 0;
  CHECK(o == NUKI_AUTH_ENTRY_DETAILS_LEN);
  CHECK(nuki_uart_parse_auth_entry(f, o, &a) == NUKI_UART_OK);
  CHECK(a.has_details == 1 && a.created.year == 2024 &&
        a.last_active.year == 2026 && a.lock_count == 321 &&
        a.time_limited == 0);
  /* Authorization Entry Count 0x0027 */
  {
    static const uint8_t c[] = {0x27, 0x00, 0x05, 0x00};
    uint16_t v = 0;
    CHECK(nuki_uart_parse_auth_count(c, sizeof(c), &v) == NUKI_UART_OK);
    CHECK(v == 5);
  }
}

/* ── Name tables and helpers ─────────────────────────────────────────── */

static void test_names(void) {
  CHECK(strcmp(nuki_lock_action_name(0x02), "Lock") == 0);
  CHECK(strcmp(nuki_lock_action_name(0x81), "FobAction1") == 0);
  CHECK(strcmp(nuki_lock_action_name(0x77), "Unknown") == 0);
  CHECK(strcmp(nuki_trigger_name(NUKI_TRIGGER_MANUAL), "manual") == 0);
  CHECK(strcmp(nuki_trigger_name(NUKI_TRIGGER_AUTO_LOCK), "autoLock") == 0);
  CHECK(strcmp(nuki_completion_status_name(0x01), "motorBlocked") == 0);
  CHECK(strcmp(nuki_completion_status_name(0xE1), "invalidFingerprint") == 0);
  CHECK(strcmp(nuki_door_sensor_state_name(NUKI_DOOR_CLOSED), "doorClosed") ==
        0);
  CHECK(strcmp(nuki_door_sensor_state_name(NUKI_DOOR_OPENED), "doorOpened") ==
        0);
  CHECK(strcmp(nuki_door_sensor_state_name(NUKI_DOOR_TAMPERED), "tampered") ==
        0);
  CHECK(strcmp(nuki_door_sensor_state_name(NUKI_DOOR_UNCALIBRATED),
               "uncalibrated") == 0);
  CHECK(strcmp(nuki_door_sensor_state_name(0x77), "undefined") == 0);
  CHECK(strcmp(nuki_door_log_action_name(0x03), "SensorTampered") == 0);

  {
    char dst[6];
    static const uint8_t src[] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
    nuki_uart_copy_name(dst, sizeof(dst), src, sizeof(src));
    CHECK(strcmp(dst, "abcde") == 0);
    nuki_uart_copy_name(dst, 1, src, sizeof(src));
    CHECK(dst[0] == '\0');
  }
}

int main(void) {
  test_pin();
  test_builders();
  test_log_entry();
  test_keypad_code();
  test_auth_entry();
  test_names();
  if (failures) {
    printf("%d failure(s)\n", failures);
    return EXIT_FAILURE;
  }
  printf("test_uart_entries: all checks passed\n");
  return EXIT_SUCCESS;
}
