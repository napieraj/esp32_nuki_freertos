#pragma once

/*
 * nuki_uart_seclink — host side of the bridge's authenticated, encrypted
 * UART transport (mirror of src/sec_link.h in nRF52840_nuki_bridge, v1).
 *
 * The core below is plain C (also valid C++), depends on libsodium only and
 * has no ESPHome / ESP-IDF includes, so the identical code is unit-tested
 * on the host (tests/test_seclink_host.cpp) against the bridge's pinned
 * transcript (tests/sec_link_vectors.txt on the bridge side).  The ESPHome
 * component only ever plays the INITIATOR role; the RESPONDER role is kept
 * so the host test can replay the whole transcript from both ends.
 *
 * Protocol (Noise-KK-like, see the bridge header for the rationale):
 *
 *   Pairing (once, inside the bridge's pairing window): both sides swap
 *   static X25519 public keys, [0xE3][static_pk:32], and persist the peer's.
 *   Session (every boot / resync): the host sends [0xE1][e_i:32]; the bridge
 *   answers [0xE2][e_r:32][confirm:16].  Both derive
 *     ck    = BLAKE2b-256("nukibridge-sec-v1" || DH1 || DH2 || DH3 || DH4 ||
 *                         e_i || e_r)
 *       DH1 = X25519(s_i, S_r)  DH2 = X25519(e_i, S_r)
 *       DH3 = X25519(s_i, E_r)  DH4 = X25519(e_i, E_r)        (initiator view)
 *     k_i2r = BLAKE2b-256(key = ck, "i2r")
 *     k_r2i = BLAKE2b-256(key = ck, "r2i")
 *     confirm = XChaCha20-Poly1305 tag(k_r2i, nonce = 24 x 00,
 *                                      ad = 0xE2 || e_i || e_r, pt = empty)
 *   Data: [0xE0][ctr:8 LE][tag:16][ct:n]; one 64-bit counter per direction
 *   starting at 1; nonce = ctr LE64 || 16 x 00; ad = the 9-byte header.
 *   The receiver rejects any counter <= the last accepted one.  The
 *   plaintext is the ordinary v2 command frame body [TYPE][SEQ:2 LE][DATA]
 *   (no CRC: the outer COBS/CRC framing still wraps the sec_link frame).
 *
 * Wire classification: a decoded UART frame whose first byte is in
 * 0xE0..0xE3 is a sec_link frame; everything else is plaintext.  HELLO
 * (0x7D / 0x90) is always plaintext.
 *
 * libsodium mapping (fixed by the bridge's transcript):
 *   crypto_scalarmult_curve25519_base / crypto_scalarmult_curve25519
 *   crypto_generichash(out, 32, msg, n, key, keylen)   (BLAKE2b-256)
 *   crypto_aead_xchacha20poly1305_ietf_{encrypt,decrypt}_detached
 *   sodium_memcmp / sodium_memzero
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Sizes and wire types ────────────────────────────────────────────── */

#define NUKI_SECLINK_KEY_LEN 32U
#define NUKI_SECLINK_TAG_LEN 16U
#define NUKI_SECLINK_CTR_LEN 8U
#define NUKI_SECLINK_HDR_LEN (1U + NUKI_SECLINK_CTR_LEN)
#define NUKI_SECLINK_OVERHEAD                                                  \
  (NUKI_SECLINK_HDR_LEN + NUKI_SECLINK_TAG_LEN)              /* 25 */
#define NUKI_SECLINK_HS_INIT_LEN (1U + NUKI_SECLINK_KEY_LEN) /* 33 */
#define NUKI_SECLINK_HS_RESP_LEN                                               \
  (1U + NUKI_SECLINK_KEY_LEN + NUKI_SECLINK_TAG_LEN)      /* 49 */
#define NUKI_SECLINK_PAIR_LEN (1U + NUKI_SECLINK_KEY_LEN) /* 33 */

#define NUKI_SECLINK_TYPE_DATA 0xE0U
#define NUKI_SECLINK_TYPE_HS_INIT 0xE1U
#define NUKI_SECLINK_TYPE_HS_RESP 0xE2U
#define NUKI_SECLINK_TYPE_PAIR 0xE3U

/* ── Host contract additions carried by the bridge (Phase 3) ────────── */

/* HELLO (0x90) capability bits. */
#define NUKI_UART_CAP_SEC_LINK_SUPPORTED 0x0020U
#define NUKI_UART_CAP_SEC_REQUIRED 0x0040U /* set once a host key is stored */

/*
 * Commands (host -> bridge).  PAIR_WINDOW [seconds:1] is plaintext-accepted
 * like HELLO / HS / PAIR, but only before a host key exists (bootstrap) or
 * inside an already-open window.  UNPAIR_HOST is only valid inside the
 * secure session; it wipes the bridge's stored host key.
 */
#ifndef NUKI_UART_CMD_PAIR_WINDOW
#define NUKI_UART_CMD_PAIR_WINDOW 0x77
#endif
#ifndef NUKI_UART_CMD_UNPAIR_HOST
#define NUKI_UART_CMD_UNPAIR_HOST 0x78
#endif
/* Error code: plaintext command refused because a host key is stored. */
#ifndef NUKI_UART_ERR_SECURE_REQUIRED
#define NUKI_UART_ERR_SECURE_REQUIRED 0x08
#endif

/* Return codes (same numbering as the bridge). */
#define NUKI_SECLINK_OK 0
#define NUKI_SECLINK_EINVAL (-1)  /* bad argument / buffer too small        */
#define NUKI_SECLINK_ESTATE (-2)  /* call not valid in current state        */
#define NUKI_SECLINK_EAUTH (-3)   /* tag verification failed                */
#define NUKI_SECLINK_EREPLAY (-4) /* counter not strictly increasing        */
#define NUKI_SECLINK_ETYPE (-5)   /* unknown SEC_LINK type byte             */
#define NUKI_SECLINK_ENOPEER (-6) /* no paired peer key                     */

typedef enum {
  NUKI_SECLINK_INITIATOR = 0, /* the ESPHome host */
  NUKI_SECLINK_RESPONDER = 1, /* the bridge (host test only) */
} nuki_seclink_role_t;

typedef enum {
  NUKI_SECLINK_UNPAIRED = 0, /* no peer static key                          */
  NUKI_SECLINK_PAIRED,       /* peer key known, no session                   */
  NUKI_SECLINK_HS_SENT,      /* initiator: HS_INIT sent, awaiting HS_RESP    */
  NUKI_SECLINK_READY,        /* session keys established                     */
} nuki_seclink_state_t;

typedef struct {
  uint8_t sk[NUKI_SECLINK_KEY_LEN];
  uint8_t pk[NUKI_SECLINK_KEY_LEN];
} nuki_seclink_static_t;

typedef struct {
  nuki_seclink_role_t role;
  nuki_seclink_state_t state;
  nuki_seclink_static_t self;
  uint8_t peer_pk[NUKI_SECLINK_KEY_LEN];
  uint8_t eph_sk[NUKI_SECLINK_KEY_LEN];
  uint8_t eph_pk[NUKI_SECLINK_KEY_LEN];
  uint8_t k_send[NUKI_SECLINK_KEY_LEN];
  uint8_t k_recv[NUKI_SECLINK_KEY_LEN];
  uint64_t ctr_send;
  uint64_t ctr_recv; /* last accepted */
} nuki_seclink_ctx_t;

/* Initialise libsodium once; returns true when usable. Cheap to repeat. */
bool nuki_seclink_crypto_init(void);

/* Derive a static key pair from a 32-byte random seed (sk = seed as given). */
void nuki_seclink_keygen(nuki_seclink_static_t *out,
                         const uint8_t seed[NUKI_SECLINK_KEY_LEN]);

/* Initialise ctx; state UNPAIRED, or PAIRED if peer_pk != NULL. */
void nuki_seclink_init(nuki_seclink_ctx_t *ctx, nuki_seclink_role_t role,
                       const nuki_seclink_static_t *self,
                       const uint8_t *peer_pk);

/* Store the peer's static key (TOFU; the caller enforces the window). */
int nuki_seclink_set_peer(nuki_seclink_ctx_t *ctx,
                          const uint8_t peer_pk[NUKI_SECLINK_KEY_LEN]);

/* Build our PAIR message [0xE3][our static pk]. Returns 33 or an error. */
int nuki_seclink_build_pair_msg(const nuki_seclink_ctx_t *ctx, uint8_t *out,
                                size_t out_max);

/* Initiator: build HS_INIT from a fresh seed. PAIRED/HS_SENT/READY -> HS_SENT.
 */
int nuki_seclink_handshake_init(nuki_seclink_ctx_t *ctx,
                                const uint8_t eph_seed[NUKI_SECLINK_KEY_LEN],
                                uint8_t *out, size_t out_max);

/* Responder: consume HS_INIT, build HS_RESP. -> READY on success. */
int nuki_seclink_handshake_respond(nuki_seclink_ctx_t *ctx,
                                   const uint8_t eph_seed[NUKI_SECLINK_KEY_LEN],
                                   const uint8_t *in, size_t in_len,
                                   uint8_t *out, size_t out_max);

/* Initiator: consume HS_RESP, verify the tag. HS_SENT -> READY (else PAIRED).
 */
int nuki_seclink_handshake_finish(nuki_seclink_ctx_t *ctx, const uint8_t *in,
                                  size_t in_len);

bool nuki_seclink_ready(const nuki_seclink_ctx_t *ctx);
nuki_seclink_state_t nuki_seclink_get_state(const nuki_seclink_ctx_t *ctx);
const char *nuki_seclink_state_name(nuki_seclink_state_t state);

/* Seal an inner frame. Returns the wire length or an error. Bumps ctr_send. */
int nuki_seclink_seal(nuki_seclink_ctx_t *ctx, const uint8_t *pt, size_t pt_len,
                      uint8_t *out, size_t out_max);

/*
 * Open a DATA frame: checks type, replay counter and tag (in that order).
 * Returns the plaintext length or an error.  pt_out must not overlap in.
 */
int nuki_seclink_open(nuki_seclink_ctx_t *ctx, const uint8_t *in, size_t in_len,
                      uint8_t *pt_out, size_t pt_max);

/* True for 0xE0..0xE3. */
bool nuki_seclink_is_sec_frame(uint8_t first_byte);

/* Drop the session (keys + counters wiped) -> PAIRED; keeps static + peer. */
void nuki_seclink_end_session(nuki_seclink_ctx_t *ctx);

/* Wipe everything incl. the static key -> UNPAIRED (role preserved). */
void nuki_seclink_wipe(nuki_seclink_ctx_t *ctx);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ── ESPHome-preferences-backed key store (target build only) ────────── */

#if defined(USE_ESPHOME) || defined(USE_ESP32)
namespace esphome {
namespace nuki_uart_bridge {

/*
 * Persistent host-side secrets, keyed by the lock entity's object-id hash
 * so two bridges on one ESP32 do not share keys.  The static key is
 * generated from libsodium's randombytes (esp_random on ESP-IDF) on first
 * use and never leaves flash; the peer key is what the bridge sent in its
 * PAIR message inside the pairing window (TOFU).
 */
class NukiSecLinkKeyStore {
public:
  /* `hash` = fnv1_hash(object_id) of the owning lock, or any stable id. */
  void init(uint32_t hash);

  /* Load the static key pair, generating and persisting one if absent. */
  bool load_or_create_static(nuki_seclink_static_t *out);
  /* Peer (bridge) static public key: present only after a successful PAIR. */
  bool load_peer_pk(uint8_t pk[NUKI_SECLINK_KEY_LEN]);
  bool save_peer_pk(const uint8_t pk[NUKI_SECLINK_KEY_LEN]);
  /* Forget the bridge (host-side counterpart of UNPAIR_HOST 0x78). */
  bool clear_peer_pk();

  /* 32 random bytes for an ephemeral seed (HS_INIT). */
  static bool random_seed(uint8_t seed[NUKI_SECLINK_KEY_LEN]);

protected:
  struct StaticRecord {
    uint32_t magic;
    uint8_t sk[NUKI_SECLINK_KEY_LEN];
    uint8_t pk[NUKI_SECLINK_KEY_LEN];
  };
  struct PeerRecord {
    uint32_t magic; /* 0 = no peer */
    uint8_t pk[NUKI_SECLINK_KEY_LEN];
  };
  uint32_t hash_{0};
  bool inited_{false};
};

} // namespace nuki_uart_bridge
} // namespace esphome
#endif /* USE_ESPHOME || USE_ESP32 */
