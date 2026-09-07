/*
 * Host unit test for components/nuki_uart_bridge/nuki_uart_framing.h.
 *
 * Build & run:  make test-host
 *           or: gcc -std=c99 -Wall -Wextra -Werror -Icomponents/nuki_uart_bridge \
 *                   tests/test_uart_framing.c -o /tmp/test_uart_framing && /tmp/test_uart_framing
 */

#include "nuki_uart_framing.h"

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
  printf("%-28s", label);
  for (size_t i = 0; i < n; i++) {
    printf("%02X%s", p[i], i + 1 < n ? " " : "");
  }
  printf("\n");
}

static int bytes_eq(const uint8_t *a, const uint8_t *b, size_t n) {
  return memcmp(a, b, n) == 0;
}

/* ── CRC ─────────────────────────────────────────────────────────────── */

static void test_crc(void) {
  /* Nuki API v2.3.1 p.84: Request Data(Public Key) 0x0001 / 0x0003 -> 27A7 */
  static const uint8_t v[] = {0x01, 0x00, 0x03, 0x00};
  CHECK(nuki_uart_crc16(v, sizeof(v)) == 0xA727);
  /* CRC-16/CCITT-FALSE check value for "123456789" */
  CHECK(nuki_uart_crc16((const uint8_t *)"123456789", 9) == 0x29B1);
  CHECK(nuki_uart_crc16(NULL, 0) == 0xFFFF);
}

/* ── COBS vectors (Wikipedia / Cheshire & Baker) ─────────────────────── */

static void cobs_vector(const uint8_t *in, size_t in_len, const uint8_t *exp,
                        size_t exp_len) {
  uint8_t enc[600];
  uint8_t dec[600];
  int n = nuki_uart_cobs_encode(enc, sizeof(enc), in, in_len);
  CHECK(n == (int)exp_len);
  if (n == (int)exp_len) {
    CHECK(bytes_eq(enc, exp, exp_len));
  }
  int m = nuki_uart_cobs_decode(dec, sizeof(dec), enc, (size_t)n);
  CHECK(m == (int)in_len);
  if (m == (int)in_len && in_len > 0) {
    CHECK(bytes_eq(dec, in, in_len));
  }
}

static void test_cobs(void) {
  {
    static const uint8_t in[] = {0x00}, exp[] = {0x01, 0x01};
    cobs_vector(in, 1, exp, 2);
  }
  {
    static const uint8_t in[] = {0x00, 0x00}, exp[] = {0x01, 0x01, 0x01};
    cobs_vector(in, 2, exp, 3);
  }
  {
    static const uint8_t in[] = {0x11, 0x22, 0x00, 0x33};
    static const uint8_t exp[] = {0x03, 0x11, 0x22, 0x02, 0x33};
    cobs_vector(in, 4, exp, 5);
  }
  {
    static const uint8_t in[] = {0x11, 0x22, 0x33, 0x44};
    static const uint8_t exp[] = {0x05, 0x11, 0x22, 0x33, 0x44};
    cobs_vector(in, 4, exp, 5);
  }
  {
    static const uint8_t in[] = {0x11, 0x00, 0x00, 0x00};
    static const uint8_t exp[] = {0x02, 0x11, 0x01, 0x01, 0x01};
    cobs_vector(in, 4, exp, 5);
  }
  {
    /* 254 non-zero bytes: full 0xFF block followed by an empty 0x01 block.
     * The bridge's cobs.c emits the same trailing code; both decoders accept
     * either form. */
    uint8_t in[254], exp[256];
    exp[0] = 0xFF;
    for (int i = 0; i < 254; i++) {
      in[i] = (uint8_t)(i + 1);
      exp[i + 1] = (uint8_t)(i + 1);
    }
    exp[255] = 0x01;
    cobs_vector(in, 254, exp, 256);
    /* the canonical form without the trailing code decodes identically */
    {
      uint8_t dec[254];
      CHECK(nuki_uart_cobs_decode(dec, sizeof(dec), exp, 255) == 254);
      CHECK(bytes_eq(dec, in, 254));
    }
  }
  {
    /* 255 non-zero bytes: 0xFF block + [0x02 0xFF] */
    uint8_t in[255], exp[257];
    exp[0] = 0xFF;
    for (int i = 0; i < 254; i++) {
      in[i] = (uint8_t)(i + 1);
      exp[i + 1] = (uint8_t)(i + 1);
    }
    in[254] = 0xFF;
    exp[255] = 0x02;
    exp[256] = 0xFF;
    cobs_vector(in, 255, exp, 257);
  }
  {
    /* empty input encodes to a single 0x01 */
    static const uint8_t exp[] = {0x01};
    cobs_vector(NULL, 0, exp, 1);
  }

  /* malformed / overflow */
  {
    uint8_t dec[16];
    static const uint8_t zero_code[] = {0x00, 0x11};
    CHECK(nuki_uart_cobs_decode(dec, sizeof(dec), zero_code, 2) ==
          NUKI_UART_ERR_MALFORMED);
    static const uint8_t truncated[] = {0x05, 0x11, 0x22};
    CHECK(nuki_uart_cobs_decode(dec, sizeof(dec), truncated, 3) ==
          NUKI_UART_ERR_MALFORMED);
    static const uint8_t big[] = {0x05, 0x11, 0x22, 0x33, 0x44};
    CHECK(nuki_uart_cobs_decode(dec, 3, big, 5) == NUKI_UART_ERR_OVERFLOW);
    uint8_t enc[4];
    CHECK(nuki_uart_cobs_encode(enc, sizeof(enc), big, 5) ==
          NUKI_UART_ERR_OVERFLOW);
  }

  /* pseudo-random round trips, bounded by the largest frame we transmit */
  {
    uint8_t in[NUKI_UART_FRAME_MAX], enc[NUKI_UART_WIRE_MAX], dec[NUKI_UART_FRAME_MAX];
    uint32_t s = 0x12345678U;
    for (int iter = 0; iter < 2000; iter++) {
      size_t n = (size_t)(s % (NUKI_UART_FRAME_MAX + 1));
      for (size_t i = 0; i < n; i++) {
        s = s * 1103515245U + 12345U;
        /* bias towards zeros to exercise block boundaries */
        in[i] = ((s >> 16) & 3U) == 0U ? 0x00 : (uint8_t)(s >> 24);
      }
      int e = nuki_uart_cobs_encode(enc, sizeof(enc), in, n);
      CHECK(e > 0);
      for (int i = 0; i < e; i++) {
        CHECK(enc[i] != 0x00);
      }
      int d = nuki_uart_cobs_decode(dec, sizeof(dec), enc, (size_t)e);
      CHECK(d == (int)n);
      if (d == (int)n && n > 0) {
        CHECK(bytes_eq(dec, in, n));
      }
      s = s * 1103515245U + 12345U;
    }
  }
}

/* ── Frame build / parse ─────────────────────────────────────────────── */

static void test_frames(void) {
  uint8_t wire[NUKI_UART_WIRE_MAX];
  uint8_t dec[NUKI_UART_FRAME_MAX];
  nuki_uart_msg_t msg;

  /* HELLO: v1 [7D][02][CRC] -> CRC(7D 02) */
  {
    static const uint8_t body[] = {0x7D, 0x02};
    uint16_t crc = nuki_uart_crc16(body, 2);
    int n = nuki_uart_build_hello(wire, sizeof(wire), 0x02);
    CHECK(n == 7);
    hexdump("HELLO wire:", wire, (size_t)n);
    CHECK(wire[0] == 0x00 && wire[n - 1] == 0x00);
    /* no zeros in the body, so a single block: 05 7D 02 crc_lo crc_hi */
    CHECK(wire[1] == 0x05 && wire[2] == 0x7D && wire[3] == 0x02);
    CHECK(wire[4] == (uint8_t)crc && wire[5] == (uint8_t)(crc >> 8));

    int d = nuki_uart_cobs_decode(dec, sizeof(dec), wire + 1, (size_t)n - 2);
    CHECK(d == 4);
    CHECK(nuki_uart_parse_frame(dec, (size_t)d, NUKI_UART_PROTO_V1, &msg) ==
          NUKI_UART_OK);
    CHECK(msg.type == NUKI_UART_CMD_HELLO && msg.seq == 0 && msg.len == 1 &&
          msg.data[0] == 0x02);
  }

  /* UNLOCK v2 seq=1: [01][01 00][CRC] */
  {
    static const uint8_t body[] = {0x01, 0x01, 0x00};
    uint16_t crc = nuki_uart_crc16(body, 3);
    int n = nuki_uart_build_frame(wire, sizeof(wire), NUKI_UART_PROTO_V2,
                                  NUKI_UART_CMD_UNLOCK, 1U, NULL, 0);
    CHECK(n > 0);
    hexdump("UNLOCK seq=1 wire:", wire, (size_t)n);
    printf("%-28s%04X (LE %02X %02X)\n", "UNLOCK seq=1 CRC:", crc,
           (uint8_t)crc, (uint8_t)(crc >> 8));
    int d = nuki_uart_cobs_decode(dec, sizeof(dec), wire + 1, (size_t)n - 2);
    CHECK(d == 5);
    CHECK(dec[0] == 0x01 && dec[1] == 0x01 && dec[2] == 0x00);
    CHECK(dec[3] == (uint8_t)crc && dec[4] == (uint8_t)(crc >> 8));
    CHECK(nuki_uart_parse_frame(dec, (size_t)d, NUKI_UART_PROTO_V2, &msg) ==
          NUKI_UART_OK);
    CHECK(msg.type == NUKI_UART_CMD_UNLOCK && msg.seq == 1 && msg.len == 0);
    /* same frame parsed as v1 must fail the CRC-free structural check: it
     * would be type 0x01 with DATA=[01 00] and still a valid CRC, so the
     * host must know the negotiated version — verify we at least get the
     * expected v1 split. */
    CHECK(nuki_uart_parse_frame(dec, (size_t)d, NUKI_UART_PROTO_V1, &msg) ==
          NUKI_UART_OK);
    CHECK(msg.seq == 0 && msg.len == 2);
  }

  /* LOCK v2 seq=0x1234 with payload containing zeros round-trips */
  {
    static const uint8_t payload[] = {0x00, 0xAA, 0x00, 0x00, 0xBB};
    int n = nuki_uart_build_frame(wire, sizeof(wire), NUKI_UART_PROTO_V2,
                                  NUKI_UART_CMD_LOCK, 0x1234U, payload,
                                  sizeof(payload));
    CHECK(n > 0);
    for (int i = 1; i < n - 1; i++) {
      CHECK(wire[i] != 0x00);
    }
    int d = nuki_uart_cobs_decode(dec, sizeof(dec), wire + 1, (size_t)n - 2);
    CHECK(d == 3 + 5 + 2);
    CHECK(nuki_uart_parse_frame(dec, (size_t)d, NUKI_UART_PROTO_V2, &msg) ==
          NUKI_UART_OK);
    CHECK(msg.type == NUKI_UART_CMD_LOCK && msg.seq == 0x1234 &&
          msg.len == sizeof(payload) && bytes_eq(msg.data, payload, msg.len));
  }

  /* CRC corruption is rejected */
  {
    int n = nuki_uart_build_frame(wire, sizeof(wire), NUKI_UART_PROTO_V2,
                                  NUKI_UART_CMD_PING, 7U, NULL, 0);
    int d = nuki_uart_cobs_decode(dec, sizeof(dec), wire + 1, (size_t)n - 2);
    dec[0] ^= 0x40;
    CHECK(nuki_uart_parse_frame(dec, (size_t)d, NUKI_UART_PROTO_V2, &msg) ==
          NUKI_UART_ERR_CRC);
    CHECK(nuki_uart_parse_frame(dec, 2, NUKI_UART_PROTO_V2, &msg) ==
          NUKI_UART_ERR_SHORT);
  }

  /* oversized payload is refused before touching the output */
  {
    uint8_t big[NUKI_UART_FRAME_MAX];
    memset(big, 0x5A, sizeof(big));
    CHECK(nuki_uart_build_frame(wire, sizeof(wire), NUKI_UART_PROTO_V2,
                                NUKI_UART_CMD_LOCK, 1U, big,
                                NUKI_UART_FRAME_MAX - 4) ==
          NUKI_UART_ERR_OVERFLOW);
    CHECK(nuki_uart_build_frame(wire, sizeof(wire), NUKI_UART_PROTO_V2,
                                NUKI_UART_CMD_LOCK, 1U, big,
                                NUKI_UART_FRAME_MAX - 5) > 0);
  }

  /* HELLO responses: boot hello (v1, selected=1) while host is in v2 */
  {
    uint8_t f[9];
    uint16_t crc;
    /* v1: [90][02 01 1D 00][crc] */
    f[0] = 0x90;
    f[1] = 0x02;
    f[2] = 0x01;
    f[3] = 0x1D;
    f[4] = 0x00;
    crc = nuki_uart_crc16(f, 5);
    f[5] = (uint8_t)crc;
    f[6] = (uint8_t)(crc >> 8);
    CHECK(nuki_uart_parse_frame(f, 7, NUKI_UART_PROTO_V2, &msg) == NUKI_UART_OK);
    CHECK(msg.type == 0x90 && msg.seq == 0 && msg.len == 4 &&
          msg.data[0] == 0x02 && msg.data[1] == 0x01 &&
          nuki_uart_get_u16(msg.data + 2) == 0x001D);
    /* v2 reply: [90][00 00][02 02 1D 00][crc] */
    f[0] = 0x90;
    f[1] = 0x00;
    f[2] = 0x00;
    f[3] = 0x02;
    f[4] = 0x02;
    f[5] = 0x1D;
    f[6] = 0x00;
    crc = nuki_uart_crc16(f, 7);
    f[7] = (uint8_t)crc;
    f[8] = (uint8_t)(crc >> 8);
    CHECK(nuki_uart_parse_frame(f, 9, NUKI_UART_PROTO_V1, &msg) == NUKI_UART_OK);
    CHECK(msg.type == 0x90 && msg.seq == 0 && msg.len == 4 &&
          msg.data[0] == 0x02 && msg.data[1] == 0x02);
  }
}

/* ── Payload helpers ─────────────────────────────────────────────────── */

static void test_payloads(void) {
  const uint8_t *body = NULL;

  /* forwarded Keyturner States: [0C 00][nuki_state][lock_state]... */
  {
    static const uint8_t p[] = {0x0C, 0x00, 0x02, 0x01, 0x00, 0xE9, 0x07};
    size_t n = nuki_uart_keyturner_body(p, sizeof(p), &body);
    CHECK(n == 5 && body == p + 2 && body[0] == 0x02 && body[1] == 0x01);
  }
  /* bare payload without cmd id */
  {
    static const uint8_t p[] = {0x02, 0x03, 0x00};
    size_t n = nuki_uart_keyturner_body(p, sizeof(p), &body);
    CHECK(n == 3 && body == p && body[1] == 0x03);
  }
  /* Status (0x000E) and short buffers are not Keyturner States */
  {
    static const uint8_t st[] = {0x0E, 0x00, 0x01};
    CHECK(nuki_uart_keyturner_body(st, sizeof(st), &body) == 0);
    static const uint8_t one[] = {0x01};
    CHECK(nuki_uart_keyturner_body(one, 1, &body) == 0);
    CHECK(nuki_uart_keyturner_body(NULL, 0, &body) == 0);
  }

  /* PAIR payload prefix trimming */
  {
    uint8_t out[10];
    CHECK(nuki_uart_build_pair_payload(out, 0, 0, 0, 0) == 0);
    CHECK(nuki_uart_build_pair_payload(out, NUKI_UART_DEVICE_ULTRA, 0, 0, 0) ==
          1);
    CHECK(out[0] == NUKI_UART_DEVICE_ULTRA);
    CHECK(nuki_uart_build_pair_payload(out, 0, 65432, 0, 0) == 5);
    CHECK(out[0] == 0 && nuki_uart_get_u32(out + 1) == 65432);
    CHECK(nuki_uart_build_pair_payload(out, 0, 0, NUKI_UART_ID_TYPE_BRIDGE,
                                       0) == 6);
    CHECK(out[5] == NUKI_UART_ID_TYPE_BRIDGE);
    CHECK(nuki_uart_build_pair_payload(out, NUKI_UART_DEVICE_CLASSIC, 123456,
                                       NUKI_UART_ID_TYPE_APP, 2020002) == 10);
    CHECK(out[0] == 1 && nuki_uart_get_u32(out + 1) == 123456 && out[5] == 0 &&
          nuki_uart_get_u32(out + 6) == 2020002);
    hexdump("PAIR payload (full):", out, 10);
  }
}

int main(void) {
  test_crc();
  test_cobs();
  test_frames();
  test_payloads();
  if (failures) {
    printf("%d FAILURE(S)\n", failures);
    return EXIT_FAILURE;
  }
  printf("all nuki_uart_framing tests passed\n");
  return EXIT_SUCCESS;
}
