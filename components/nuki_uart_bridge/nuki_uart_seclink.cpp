/*
 * nuki_uart_seclink — host side of the bridge's secure UART transport.
 *
 * See nuki_uart_seclink.h for the protocol.  This translation unit is a
 * line-for-line mirror of src/sec_link.c on the bridge with the Monocypher
 * primitives replaced by their libsodium equivalents:
 *
 *   crypto_x25519_public_key(pk, sk) -> crypto_scalarmult_curve25519_base
 *   crypto_x25519(out, sk, pk)       -> crypto_scalarmult_curve25519
 *   crypto_blake2b(h, 32, m, n)      -> crypto_generichash(h,32,m,n,NULL,0)
 *   crypto_blake2b_keyed(h,32,k,32,m,n)
 *                                    -> crypto_generichash(h,32,m,n,k,32)
 *   crypto_aead_lock / _unlock       -> crypto_aead_xchacha20poly1305_ietf_
 *                                       encrypt_detached / decrypt_detached
 *   crypto_verify16                  -> sodium_memcmp
 *   crypto_wipe                      -> sodium_memzero
 *
 * Both X25519 implementations clamp the scalar on use, so the static and
 * ephemeral secret keys are stored exactly as seeded.  libsodium's
 * crypto_scalarmult_curve25519 additionally returns -1 for an all-zero
 * result; the explicit zero check is kept so the two sides fail the same way.
 *
 * The core is compiled on the host by tests/test_seclink_host.cpp with the
 * system libsodium; on the ESP32 it needs the espressif/libsodium IDF
 * component (add_idf_component in lock.py).
 */
#include "nuki_uart_seclink.h"

#include <string.h>

#if defined(__has_include)
#if !__has_include(<sodium.h>)
#error                                                                         \
    "nuki_uart_seclink needs libsodium: add_idf_component(name=\"espressif/libsodium\", ref=\"^1.0.20~2\") in lock.py"
#endif
#endif
#include <sodium.h>

/* Domain separation for the chaining key; no terminating NUL on the wire. */
static const uint8_t HS_PREFIX[] = {'n', 'u', 'k', 'i', 'b', 'r', 'i', 'd', 'g',
                                    'e', '-', 's', 'e', 'c', '-', 'v', '1'};
static const uint8_t LABEL_I2R[] = {'i', '2', 'r'};
static const uint8_t LABEL_R2I[] = {'r', '2', 'i'};

/* Nonce for the confirmation tag: counter 0, i.e. 24 zero bytes. */
static const uint8_t ZERO_NONCE[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES] =
    {0};

static void put_le64(uint8_t out[8], uint64_t v) {
  for (size_t i = 0; i < 8; i++) {
    out[i] = (uint8_t)(v >> (8 * i));
  }
}

static uint64_t get_le64(const uint8_t in[8]) {
  uint64_t v = 0;
  for (size_t i = 0; i < 8; i++) {
    v |= (uint64_t)in[i] << (8 * i);
  }
  return v;
}

/*
 * Constant-time "is all zero" over 32 bytes: rejects shared secrets produced
 * by low-order peer points (an attacker-chosen public key that maps every
 * scalar to the identity).
 */
static int is_zero32(const uint8_t p[32]) {
  uint8_t acc = 0;
  for (size_t i = 0; i < 32; i++) {
    acc |= p[i];
  }
  return acc == 0;
}

static void x25519_base(uint8_t pk[32], const uint8_t sk[32]) {
  (void)crypto_scalarmult_curve25519_base(pk, sk);
}

/* Shared secret; the all-zero case is caught by the caller via is_zero32. */
static void x25519(uint8_t out[32], const uint8_t sk[32],
                   const uint8_t pk[32]) {
  if (crypto_scalarmult_curve25519(out, sk, pk) != 0) {
    memset(out, 0, 32);
  }
}

static void wipe_session(nuki_seclink_ctx_t *ctx) {
  sodium_memzero(ctx->eph_sk, sizeof(ctx->eph_sk));
  sodium_memzero(ctx->eph_pk, sizeof(ctx->eph_pk));
  sodium_memzero(ctx->k_send, sizeof(ctx->k_send));
  sodium_memzero(ctx->k_recv, sizeof(ctx->k_recv));
  ctx->ctr_send = 0;
  ctx->ctr_recv = 0;
}

/*
 * Derive the session keys from the four DH results plus both ephemerals.
 * The chaining-key input is, from the INITIATOR's point of view,
 *   prefix || DH(s_i,S_r) || DH(e_i,S_r) || DH(s_i,E_r) || DH(e_i,E_r) || e_i
 * || e_r and the responder feeds the same bytes in the same order:
 *
 *   slot | initiator side    | responder side
 *   -----+-------------------+-------------------
 *     1  | x25519(s_i, S_r)  | x25519(s_r, S_i)
 *     2  | x25519(e_i, S_r)  | x25519(s_r, E_i)
 *     3  | x25519(s_i, E_r)  | x25519(e_r, S_i)
 *     4  | x25519(e_i, E_r)  | x25519(e_r, E_i)
 *
 * Any all-zero DH output (low-order peer point) aborts with EAUTH.
 */
static int derive_keys(const nuki_seclink_ctx_t *ctx,
                       const uint8_t e_i[NUKI_SECLINK_KEY_LEN],
                       const uint8_t e_r[NUKI_SECLINK_KEY_LEN],
                       uint8_t k_i2r[NUKI_SECLINK_KEY_LEN],
                       uint8_t k_r2i[NUKI_SECLINK_KEY_LEN]) {
  uint8_t buf[sizeof(HS_PREFIX) + 4 * NUKI_SECLINK_KEY_LEN +
              2 * NUKI_SECLINK_KEY_LEN];
  uint8_t ck[NUKI_SECLINK_KEY_LEN];
  uint8_t *dh = buf + sizeof(HS_PREFIX);
  int bad = 0;

  memcpy(buf, HS_PREFIX, sizeof(HS_PREFIX));

  if (ctx->role == NUKI_SECLINK_INITIATOR) {
    x25519(dh + 0 * 32, ctx->self.sk, ctx->peer_pk);
    x25519(dh + 1 * 32, ctx->eph_sk, ctx->peer_pk);
    x25519(dh + 2 * 32, ctx->self.sk, e_r);
    x25519(dh + 3 * 32, ctx->eph_sk, e_r);
  } else {
    x25519(dh + 0 * 32, ctx->self.sk, ctx->peer_pk);
    x25519(dh + 1 * 32, ctx->self.sk, e_i);
    x25519(dh + 2 * 32, ctx->eph_sk, ctx->peer_pk);
    x25519(dh + 3 * 32, ctx->eph_sk, e_i);
  }
  for (size_t i = 0; i < 4; i++) {
    bad |= is_zero32(dh + i * 32);
  }
  memcpy(dh + 4 * 32, e_i, NUKI_SECLINK_KEY_LEN);
  memcpy(dh + 5 * 32, e_r, NUKI_SECLINK_KEY_LEN);

  if (bad) {
    sodium_memzero(buf, sizeof(buf));
    return NUKI_SECLINK_EAUTH;
  }

  crypto_generichash(ck, NUKI_SECLINK_KEY_LEN, buf, sizeof(buf), NULL, 0);
  crypto_generichash(k_i2r, NUKI_SECLINK_KEY_LEN, LABEL_I2R, sizeof(LABEL_I2R),
                     ck, NUKI_SECLINK_KEY_LEN);
  crypto_generichash(k_r2i, NUKI_SECLINK_KEY_LEN, LABEL_R2I, sizeof(LABEL_R2I),
                     ck, NUKI_SECLINK_KEY_LEN);

  sodium_memzero(buf, sizeof(buf));
  sodium_memzero(ck, sizeof(ck));
  return NUKI_SECLINK_OK;
}

/* confirm = AEAD tag (empty pt) under k_r2i, ctr 0, ad = HS_RESP||e_i||e_r. */
static void confirm_tag(uint8_t tag[NUKI_SECLINK_TAG_LEN],
                        const uint8_t k_r2i[NUKI_SECLINK_KEY_LEN],
                        const uint8_t e_i[NUKI_SECLINK_KEY_LEN],
                        const uint8_t e_r[NUKI_SECLINK_KEY_LEN]) {
  uint8_t ad[1 + 2 * NUKI_SECLINK_KEY_LEN];
  uint8_t none[1] = {0};
  unsigned long long tag_len = 0;

  ad[0] = NUKI_SECLINK_TYPE_HS_RESP;
  memcpy(ad + 1, e_i, NUKI_SECLINK_KEY_LEN);
  memcpy(ad + 1 + NUKI_SECLINK_KEY_LEN, e_r, NUKI_SECLINK_KEY_LEN);

  crypto_aead_xchacha20poly1305_ietf_encrypt_detached(
      none, tag, &tag_len, none, 0, ad, sizeof(ad), NULL, ZERO_NONCE, k_r2i);
}

bool nuki_seclink_crypto_init(void) { return sodium_init() >= 0; }

void nuki_seclink_keygen(nuki_seclink_static_t *out,
                         const uint8_t seed[NUKI_SECLINK_KEY_LEN]) {
  if (out == NULL || seed == NULL) {
    return;
  }
  memcpy(out->sk, seed, NUKI_SECLINK_KEY_LEN);
  x25519_base(out->pk, out->sk);
}

void nuki_seclink_init(nuki_seclink_ctx_t *ctx, nuki_seclink_role_t role,
                       const nuki_seclink_static_t *self,
                       const uint8_t *peer_pk) {
  if (ctx == NULL) {
    return;
  }
  memset(ctx, 0, sizeof(*ctx));
  ctx->role = role;
  ctx->state = NUKI_SECLINK_UNPAIRED;
  if (self != NULL) {
    ctx->self = *self;
  }
  if (peer_pk != NULL) {
    (void)nuki_seclink_set_peer(ctx, peer_pk);
  }
}

int nuki_seclink_set_peer(nuki_seclink_ctx_t *ctx,
                          const uint8_t peer_pk[NUKI_SECLINK_KEY_LEN]) {
  if (ctx == NULL || peer_pk == NULL || is_zero32(peer_pk)) {
    return NUKI_SECLINK_EINVAL;
  }
  /* A new peer key invalidates whatever session was running. */
  wipe_session(ctx);
  memcpy(ctx->peer_pk, peer_pk, NUKI_SECLINK_KEY_LEN);
  ctx->state = NUKI_SECLINK_PAIRED;
  return NUKI_SECLINK_OK;
}

int nuki_seclink_build_pair_msg(const nuki_seclink_ctx_t *ctx, uint8_t *out,
                                size_t out_max) {
  if (ctx == NULL || out == NULL || out_max < NUKI_SECLINK_PAIR_LEN) {
    return NUKI_SECLINK_EINVAL;
  }
  out[0] = NUKI_SECLINK_TYPE_PAIR;
  memcpy(out + 1, ctx->self.pk, NUKI_SECLINK_KEY_LEN);
  return (int)NUKI_SECLINK_PAIR_LEN;
}

/*
 * A new handshake may be started from PAIRED, from HS_SENT (the previous
 * HS_RESP never arrived) and from READY (resync after a bridge reboot); in
 * every case the old session is discarded first.
 */
int nuki_seclink_handshake_init(nuki_seclink_ctx_t *ctx,
                                const uint8_t eph_seed[NUKI_SECLINK_KEY_LEN],
                                uint8_t *out, size_t out_max) {
  if (ctx == NULL || eph_seed == NULL || out == NULL ||
      out_max < NUKI_SECLINK_HS_INIT_LEN) {
    return NUKI_SECLINK_EINVAL;
  }
  if (ctx->role != NUKI_SECLINK_INITIATOR) {
    return NUKI_SECLINK_ESTATE;
  }
  if (ctx->state == NUKI_SECLINK_UNPAIRED) {
    return NUKI_SECLINK_ENOPEER;
  }

  wipe_session(ctx);
  memcpy(ctx->eph_sk, eph_seed, NUKI_SECLINK_KEY_LEN);
  x25519_base(ctx->eph_pk, ctx->eph_sk);

  out[0] = NUKI_SECLINK_TYPE_HS_INIT;
  memcpy(out + 1, ctx->eph_pk, NUKI_SECLINK_KEY_LEN);
  ctx->state = NUKI_SECLINK_HS_SENT;
  return (int)NUKI_SECLINK_HS_INIT_LEN;
}

/*
 * Responder side (bridge behaviour, used by the host test only): HS_INIT is
 * accepted from PAIRED and from READY and always replaces the session.
 */
int nuki_seclink_handshake_respond(nuki_seclink_ctx_t *ctx,
                                   const uint8_t eph_seed[NUKI_SECLINK_KEY_LEN],
                                   const uint8_t *in, size_t in_len,
                                   uint8_t *out, size_t out_max) {
  uint8_t k_i2r[NUKI_SECLINK_KEY_LEN];
  uint8_t k_r2i[NUKI_SECLINK_KEY_LEN];
  const uint8_t *e_i;
  int rc;

  if (ctx == NULL || eph_seed == NULL || in == NULL || out == NULL ||
      out_max < NUKI_SECLINK_HS_RESP_LEN || in_len < 1) {
    return NUKI_SECLINK_EINVAL;
  }
  if (in[0] != NUKI_SECLINK_TYPE_HS_INIT) {
    return NUKI_SECLINK_ETYPE;
  }
  if (in_len != NUKI_SECLINK_HS_INIT_LEN) {
    return NUKI_SECLINK_EINVAL;
  }
  if (ctx->role != NUKI_SECLINK_RESPONDER) {
    return NUKI_SECLINK_ESTATE;
  }
  if (ctx->state == NUKI_SECLINK_UNPAIRED) {
    return NUKI_SECLINK_ENOPEER;
  }
  e_i = in + 1;

  wipe_session(ctx);
  memcpy(ctx->eph_sk, eph_seed, NUKI_SECLINK_KEY_LEN);
  x25519_base(ctx->eph_pk, ctx->eph_sk);

  rc = derive_keys(ctx, e_i, ctx->eph_pk, k_i2r, k_r2i);
  if (rc != NUKI_SECLINK_OK) {
    wipe_session(ctx);
    ctx->state = NUKI_SECLINK_PAIRED;
    return rc;
  }

  out[0] = NUKI_SECLINK_TYPE_HS_RESP;
  memcpy(out + 1, ctx->eph_pk, NUKI_SECLINK_KEY_LEN);
  confirm_tag(out + 1 + NUKI_SECLINK_KEY_LEN, k_r2i, e_i, ctx->eph_pk);

  memcpy(ctx->k_send, k_r2i, NUKI_SECLINK_KEY_LEN);
  memcpy(ctx->k_recv, k_i2r, NUKI_SECLINK_KEY_LEN);
  sodium_memzero(k_i2r, sizeof(k_i2r));
  sodium_memzero(k_r2i, sizeof(k_r2i));
  sodium_memzero(ctx->eph_sk, sizeof(ctx->eph_sk));
  ctx->state = NUKI_SECLINK_READY;
  return (int)NUKI_SECLINK_HS_RESP_LEN;
}

/*
 * Malformed input (wrong type / length) is reported without touching the
 * state: it is not an answer to our HS_INIT, so we keep waiting for one.
 * A well-formed HS_RESP that fails the confirmation check ends the attempt
 * (-> PAIRED, session wiped); the caller decides whether to retry.
 */
int nuki_seclink_handshake_finish(nuki_seclink_ctx_t *ctx, const uint8_t *in,
                                  size_t in_len) {
  uint8_t k_i2r[NUKI_SECLINK_KEY_LEN];
  uint8_t k_r2i[NUKI_SECLINK_KEY_LEN];
  uint8_t tag[NUKI_SECLINK_TAG_LEN];
  const uint8_t *e_r;
  int rc;

  if (ctx == NULL || in == NULL || in_len < 1) {
    return NUKI_SECLINK_EINVAL;
  }
  if (ctx->role != NUKI_SECLINK_INITIATOR ||
      ctx->state != NUKI_SECLINK_HS_SENT) {
    return NUKI_SECLINK_ESTATE;
  }
  if (in[0] != NUKI_SECLINK_TYPE_HS_RESP) {
    return NUKI_SECLINK_ETYPE;
  }
  if (in_len != NUKI_SECLINK_HS_RESP_LEN) {
    return NUKI_SECLINK_EINVAL;
  }
  e_r = in + 1;

  rc = derive_keys(ctx, ctx->eph_pk, e_r, k_i2r, k_r2i);
  if (rc == NUKI_SECLINK_OK) {
    confirm_tag(tag, k_r2i, ctx->eph_pk, e_r);
    if (sodium_memcmp(tag, in + 1 + NUKI_SECLINK_KEY_LEN,
                      NUKI_SECLINK_TAG_LEN) != 0) {
      rc = NUKI_SECLINK_EAUTH;
    }
    sodium_memzero(tag, sizeof(tag));
  }

  if (rc != NUKI_SECLINK_OK) {
    sodium_memzero(k_i2r, sizeof(k_i2r));
    sodium_memzero(k_r2i, sizeof(k_r2i));
    wipe_session(ctx);
    ctx->state = NUKI_SECLINK_PAIRED;
    return rc;
  }

  memcpy(ctx->k_send, k_i2r, NUKI_SECLINK_KEY_LEN);
  memcpy(ctx->k_recv, k_r2i, NUKI_SECLINK_KEY_LEN);
  sodium_memzero(k_i2r, sizeof(k_i2r));
  sodium_memzero(k_r2i, sizeof(k_r2i));
  sodium_memzero(ctx->eph_sk, sizeof(ctx->eph_sk));
  ctx->state = NUKI_SECLINK_READY;
  return NUKI_SECLINK_OK;
}

bool nuki_seclink_ready(const nuki_seclink_ctx_t *ctx) {
  return ctx != NULL && ctx->state == NUKI_SECLINK_READY;
}

nuki_seclink_state_t nuki_seclink_get_state(const nuki_seclink_ctx_t *ctx) {
  return ctx != NULL ? ctx->state : NUKI_SECLINK_UNPAIRED;
}

const char *nuki_seclink_state_name(nuki_seclink_state_t state) {
  switch (state) {
  case NUKI_SECLINK_UNPAIRED:
    return "unpaired";
  case NUKI_SECLINK_PAIRED:
    return "paired";
  case NUKI_SECLINK_HS_SENT:
    return "handshake";
  case NUKI_SECLINK_READY:
    return "ready";
  default:
    return "?";
  }
}

/* nonce = ctr LE64 || 16 zero bytes; ad = the 9-byte wire header. */
static void make_nonce(uint8_t nonce[24], uint64_t ctr) {
  memset(nonce, 0, 24);
  put_le64(nonce, ctr);
}

int nuki_seclink_seal(nuki_seclink_ctx_t *ctx, const uint8_t *pt, size_t pt_len,
                      uint8_t *out, size_t out_max) {
  uint8_t nonce[24];
  uint8_t none[1] = {0};
  unsigned long long tag_len = 0;

  if (ctx == NULL || out == NULL || (pt == NULL && pt_len > 0) ||
      pt_len > (size_t)INT32_MAX - NUKI_SECLINK_OVERHEAD ||
      out_max < pt_len + NUKI_SECLINK_OVERHEAD) {
    return NUKI_SECLINK_EINVAL;
  }
  if (ctx->state != NUKI_SECLINK_READY) {
    return NUKI_SECLINK_ESTATE;
  }
  /* Never reuse a nonce: a wrapped counter needs a fresh handshake. */
  if (ctx->ctr_send == UINT64_MAX) {
    return NUKI_SECLINK_ESTATE;
  }
  ctx->ctr_send++;

  out[0] = NUKI_SECLINK_TYPE_DATA;
  put_le64(out + 1, ctx->ctr_send);
  make_nonce(nonce, ctx->ctr_send);
  crypto_aead_xchacha20poly1305_ietf_encrypt_detached(
      pt_len > 0 ? out + NUKI_SECLINK_OVERHEAD : none,
      out + NUKI_SECLINK_HDR_LEN, &tag_len, pt_len > 0 ? pt : none, pt_len, out,
      NUKI_SECLINK_HDR_LEN, NULL, nonce, ctx->k_send);
  return (int)(pt_len + NUKI_SECLINK_OVERHEAD);
}

/*
 * The replay check runs before the tag check so a replayed frame is reported
 * as EREPLAY rather than EAUTH; a frame whose header was tampered to a higher
 * counter fails the tag (the header is associated data) and leaves ctr_recv
 * untouched.
 */
int nuki_seclink_open(nuki_seclink_ctx_t *ctx, const uint8_t *in, size_t in_len,
                      uint8_t *pt_out, size_t pt_max) {
  uint8_t nonce[24];
  uint8_t none[1] = {0};
  uint64_t ctr;
  size_t ct_len;

  if (ctx == NULL || in == NULL || in_len < 1) {
    return NUKI_SECLINK_EINVAL;
  }
  if (in[0] != NUKI_SECLINK_TYPE_DATA) {
    return NUKI_SECLINK_ETYPE;
  }
  if (in_len < NUKI_SECLINK_OVERHEAD) {
    return NUKI_SECLINK_EINVAL;
  }
  ct_len = in_len - NUKI_SECLINK_OVERHEAD;
  if ((pt_out == NULL && ct_len > 0) || pt_max < ct_len ||
      ct_len > (size_t)INT32_MAX) {
    return NUKI_SECLINK_EINVAL;
  }
  if (ctx->state != NUKI_SECLINK_READY) {
    return NUKI_SECLINK_ESTATE;
  }

  ctr = get_le64(in + 1);
  if (ctr <= ctx->ctr_recv) {
    return NUKI_SECLINK_EREPLAY;
  }

  make_nonce(nonce, ctr);
  if (crypto_aead_xchacha20poly1305_ietf_decrypt_detached(
          ct_len > 0 ? pt_out : none, NULL, in + NUKI_SECLINK_OVERHEAD, ct_len,
          in + NUKI_SECLINK_HDR_LEN, in, NUKI_SECLINK_HDR_LEN, nonce,
          ctx->k_recv) != 0) {
    return NUKI_SECLINK_EAUTH;
  }
  ctx->ctr_recv = ctr;
  return (int)ct_len;
}

bool nuki_seclink_is_sec_frame(uint8_t first_byte) {
  return first_byte >= NUKI_SECLINK_TYPE_DATA &&
         first_byte <= NUKI_SECLINK_TYPE_PAIR;
}

void nuki_seclink_end_session(nuki_seclink_ctx_t *ctx) {
  if (ctx == NULL) {
    return;
  }
  wipe_session(ctx);
  if (ctx->state != NUKI_SECLINK_UNPAIRED) {
    ctx->state = NUKI_SECLINK_PAIRED;
  }
}

void nuki_seclink_wipe(nuki_seclink_ctx_t *ctx) {
  nuki_seclink_role_t role;

  if (ctx == NULL) {
    return;
  }
  role = ctx->role;
  sodium_memzero(ctx, sizeof(*ctx));
  ctx->role = role;
  ctx->state = NUKI_SECLINK_UNPAIRED;
}

/* ── ESPHome-preferences-backed key store (target build only) ────────── */

#if defined(USE_ESPHOME) || defined(USE_ESP32)

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace nuki_uart_bridge {

static const char *const SECLINK_TAG = "nuki_uart_bridge.seclink";

/* Distinct preference keys derived from the owner's hash. */
static const uint32_t PREF_STATIC_SALT = 0x5EC15A1Cu;
static const uint32_t PREF_PEER_SALT = 0x5EC1BEE2u;
static const uint32_t PREF_MAGIC = 0x4E534C31u; /* "NSL1" */

void NukiSecLinkKeyStore::init(uint32_t hash) {
  this->hash_ = hash;
  this->inited_ = nuki_seclink_crypto_init();
  if (!this->inited_) {
    ESP_LOGE(SECLINK_TAG, "sodium_init failed — secure link unavailable");
  }
}

bool NukiSecLinkKeyStore::random_seed(uint8_t seed[NUKI_SECLINK_KEY_LEN]) {
  if (seed == nullptr || !nuki_seclink_crypto_init()) {
    return false;
  }
  randombytes_buf(seed, NUKI_SECLINK_KEY_LEN); /* esp_random on ESP-IDF */
  return true;
}

bool NukiSecLinkKeyStore::load_or_create_static(nuki_seclink_static_t *out) {
  if (out == nullptr || !this->inited_) {
    return false;
  }
  StaticRecord rec{};
  auto pref = global_preferences->make_preference<StaticRecord>(
      this->hash_ ^ PREF_STATIC_SALT, true);
  if (pref.load(&rec) && rec.magic == PREF_MAGIC) {
    memcpy(out->sk, rec.sk, NUKI_SECLINK_KEY_LEN);
    memcpy(out->pk, rec.pk, NUKI_SECLINK_KEY_LEN);
    sodium_memzero(&rec, sizeof(rec));
    return true;
  }
  uint8_t seed[NUKI_SECLINK_KEY_LEN];
  if (!random_seed(seed)) {
    return false;
  }
  nuki_seclink_keygen(out, seed);
  sodium_memzero(seed, sizeof(seed));
  rec.magic = PREF_MAGIC;
  memcpy(rec.sk, out->sk, NUKI_SECLINK_KEY_LEN);
  memcpy(rec.pk, out->pk, NUKI_SECLINK_KEY_LEN);
  const bool ok = pref.save(&rec) && global_preferences->sync();
  sodium_memzero(&rec, sizeof(rec));
  ESP_LOGI(SECLINK_TAG, "generated host static key (%s)",
           ok ? "persisted" : "NOT persisted");
  return ok;
}

bool NukiSecLinkKeyStore::load_peer_pk(uint8_t pk[NUKI_SECLINK_KEY_LEN]) {
  if (pk == nullptr || !this->inited_) {
    return false;
  }
  PeerRecord rec{};
  auto pref = global_preferences->make_preference<PeerRecord>(
      this->hash_ ^ PREF_PEER_SALT, true);
  if (!pref.load(&rec) || rec.magic != PREF_MAGIC) {
    return false;
  }
  memcpy(pk, rec.pk, NUKI_SECLINK_KEY_LEN);
  return true;
}

bool NukiSecLinkKeyStore::save_peer_pk(const uint8_t pk[NUKI_SECLINK_KEY_LEN]) {
  if (pk == nullptr || !this->inited_) {
    return false;
  }
  PeerRecord rec{};
  rec.magic = PREF_MAGIC;
  memcpy(rec.pk, pk, NUKI_SECLINK_KEY_LEN);
  auto pref = global_preferences->make_preference<PeerRecord>(
      this->hash_ ^ PREF_PEER_SALT, true);
  return pref.save(&rec) && global_preferences->sync();
}

bool NukiSecLinkKeyStore::clear_peer_pk() {
  if (!this->inited_) {
    return false;
  }
  PeerRecord rec{}; /* magic 0 = no peer */
  auto pref = global_preferences->make_preference<PeerRecord>(
      this->hash_ ^ PREF_PEER_SALT, true);
  return pref.save(&rec) && global_preferences->sync();
}

} // namespace nuki_uart_bridge
} // namespace esphome

#endif /* USE_ESPHOME || USE_ESP32 */
