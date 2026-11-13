/*******************************************************************************
*   (c) 2020 Zondax GmbH
*   Modifications copyright 2026 Forward Research
*
*  Licensed under the Apache License, Version 2.0 (the "License");
*  you may not use this file except in compliance with the License.
*  You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS,
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
********************************************************************************/

#include "os.h"
#include "view.h"
#include "coin.h"
#include "app_main.h"
#include "crypto_store.h"
#include "zxmacros_ledger.h"
#include "os_io_seproxyhal.h"
#include "test_rsa_key.h"
#include "tx.h"

#define KEY_SLOT_1 0x00
#define MASTERSEED_LEN 32

#define SLOT_COUNT 4

static uint8_t slot_in_use;
static bool legacy_wallet = false;
static uint32_t wallet_number;
bool device_initialized = false;

typedef struct {
    uint8_t seed_hash[CX_SHA256_SIZE];
    cx_rsa_4096_public_key_t rsa_pub;
    cx_rsa_4096_private_key_t rsa_priv;
    uint64_t last_used;
    uint32_t number;
    bool legacy;
    bool initialized;
} crypto_store_t[SLOT_COUNT];

crypto_store_t NV_CONST
N_crypto_store_impl __attribute__ ((aligned(64)));
#define N_crypto_store (*(NV_VOLATILE crypto_store_t *)PIC(&N_crypto_store_impl))

typedef struct {
    uint8_t sig[RSA_MODULUS_LEN];
} crypto_sig_t[SLOT_COUNT];

crypto_sig_t NV_CONST
N_crypto_sig_impl __attribute__ ((aligned(64)));
#define N_crypto_sig (*(NV_VOLATILE crypto_sig_t *)PIC(&N_crypto_sig_impl))

uint8_t signature_set = 0;

zxerr_t crypto_store_signature(uint8_t *sig){
    MEMCPY_NV((void *)&N_crypto_sig[slot_in_use].sig, sig, RSA_MODULUS_LEN);
    signature_set = 1;
    return zxerr_ok;
}

bool is_sig_set(){
    return signature_set == 1;
}

zxerr_t crypto_signature_part(uint8_t *sig, uint8_t index){
    if (signature_set != 1){
        return zxerr_invalid_crypto_settings;
    }
    if (index > 1) {
        return zxerr_out_of_bounds;
    }
    uint8_t *start = (uint8_t *)&N_crypto_sig[slot_in_use].sig;
    MEMCPY(sig, start + index*RSA_MODULUS_HALVE, RSA_MODULUS_HALVE);
    if (index == 1){
        uint8_t dummy[RSA_MODULUS_LEN];
        MEMZERO(dummy, RSA_MODULUS_LEN);
        MEMCPY_NV((void *)&N_crypto_sig[slot_in_use].sig, dummy, RSA_MODULUS_LEN);
        signature_set = 0;
    }
    return zxerr_ok;
}

zxerr_t crypto_pubkey_part(uint8_t *key, uint8_t index){
    if (!crypto_store_is_initialized()){
        return zxerr_invalid_crypto_settings;
    }
    if (index > 1) {
        return zxerr_out_of_bounds;
    }

    cx_rsa_4096_public_key_t *rsa_pubkey = crypto_store_get_pubkey();
    if (rsa_pubkey == NULL) {
        return zxerr_invalid_crypto_settings;
    }
    const uint8_t *start = rsa_pubkey->n;

    MEMCPY(key, start + index*RSA_MODULUS_HALVE, RSA_MODULUS_HALVE);
    return zxerr_ok;
}


static cx_err_t derive_master_seed(uint8_t seed[64]) {
    const uint32_t path[] = {
        HDPATH_0_DEFAULT, HDPATH_1_DEFAULT, 0x80000000u | wallet_number, 0x80000000u, 0x80000000u
    };
    return os_derive_bip32_with_seed_no_throw(HDW_NORMAL, CX_CURVE_Ed25519,
                                             path, HDPATH_LEN_DEFAULT, seed, NULL, NULL, 0);
}

static zxerr_t derive_prime_start(uint8_t prime[RSA_PRIME_LEN], const uint8_t *seed,
                                 uint8_t index) {
    uint8_t data[2 + MASTERSEED_LEN + 1] = {legacy_wallet ? 0x00 : 0x01, index};
    MEMCPY(data + 2, seed, MASTERSEED_LEN);
    zxerr_t result = zxerr_ok;

    for (uint8_t counter = 0; counter < RSA_PRIME_LEN / CX_SHA256_SIZE; counter++) {
        data[sizeof(data) - 1] = counter;
        if (cx_hash_sha256(data, sizeof(data), prime + counter * CX_SHA256_SIZE,
                           CX_SHA256_SIZE) != CX_SHA256_SIZE) {
            result = zxerr_unknown;
            break;
        }
    }
    MEMZERO(data, sizeof(data));
    return result;
}

static bool crypto_store_slot_is_initialized(uint8_t slot) {
    return N_crypto_store[slot].initialized;
}

bool crypto_store_is_initialized() {
    return device_initialized && N_crypto_store[slot_in_use].initialized;
}

static zxerr_t crypto_init_primes(uint8_t *pq, const uint8_t *seed) {
    zemu_log_stack("initPrimes");

    view_message_show(MENU_MAIN_APP_LINE1, "Finding Pseed");
    UX_WAIT_DISPLAYED();

    volatile zxerr_t err = zxerr_unknown;
    BEGIN_TRY {
        TRY {
            io_seproxyhal_io_heartbeat();
            if (derive_prime_start(pq, seed, 0) != zxerr_ok) {
                THROW(APDU_CODE_EXECUTION_ERROR);
            }
            io_seproxyhal_io_heartbeat();

            view_message_show(MENU_MAIN_APP_LINE1, "Finding Qseed");
            UX_WAIT_DISPLAYED();
            io_seproxyhal_io_heartbeat();
            if (derive_prime_start(pq + RSA_PRIME_LEN, seed, 1) != zxerr_ok) {
                THROW(APDU_CODE_EXECUTION_ERROR);
            }
            io_seproxyhal_io_heartbeat();

            const uint8_t high_bits = legacy_wallet ? 0x80 : 0xc0;
            *pq |= high_bits;
            *(pq + RSA_PRIME_LEN) |= high_bits;
            if (!legacy_wallet) {
                pq[RSA_PRIME_LEN - 1] |= 1;
                pq[2 * RSA_PRIME_LEN - 1] |= 1;
            }

            // Obtain two prime numbers p, q inplace
            view_message_show(MENU_MAIN_APP_LINE1, "Finding P");
            UX_WAIT_DISPLAYED();
            io_seproxyhal_io_heartbeat();
            if (cx_math_next_prime_no_throw(pq, RSA_PRIME_LEN) != CX_OK) {
                THROW(APDU_CODE_EXECUTION_ERROR);
            }
            io_seproxyhal_io_heartbeat();

            view_message_show(MENU_MAIN_APP_LINE1, "Finding Q");
            UX_WAIT_DISPLAYED();
            io_seproxyhal_io_heartbeat();
            if (cx_math_next_prime_no_throw(pq + RSA_PRIME_LEN, RSA_PRIME_LEN) != CX_OK) {
                THROW(APDU_CODE_EXECUTION_ERROR);
            }
            io_seproxyhal_io_heartbeat();
            if (!legacy_wallet) {
                if ((pq[0] & 0xc0) != 0xc0 || (pq[RSA_PRIME_LEN] & 0xc0) != 0xc0) {
                    THROW(APDU_CODE_EXECUTION_ERROR);
                }
                uint8_t distance[RSA_PRIME_LEN] = {0};
                const uint8_t *p = pq;
                const uint8_t *q = pq + RSA_PRIME_LEN;
                const int order = MEMCMP(p, q, RSA_PRIME_LEN);
                const cx_err_t result = cx_math_sub_no_throw(
                    distance, order > 0 ? p : q, order > 0 ? q : p, RSA_PRIME_LEN);
                // 2^1948 is 0x10 at byte 12 of a 256-byte big-endian integer.
                const uint8_t minimum[RSA_PRIME_LEN] = {[12] = 0x10};
                const bool valid = result == CX_OK && MEMCMP(distance, minimum, sizeof(distance)) > 0;
                MEMZERO(distance, sizeof(distance));
                if (!valid) {
                    THROW(APDU_CODE_EXECUTION_ERROR);
                }
            }
            err = zxerr_ok;
        }
        CATCH_ALL {
            err = zxerr_unknown;
        }
        FINALLY {
        }
    }
    END_TRY;

    return err;
}

#ifndef APP_TESTING
static cx_err_t validate_standard_exponent(const uint8_t *pq) {
    cx_err_t error = CX_OK;
    cx_bn_t p, q, gcd, remainder, divisor, lambda;
    uint32_t gcd_bits, phi_bits;
    int compare;

    CX_CHECK(cx_bn_lock(RSA_PRIME_LEN, 0));
    CX_CHECK(cx_bn_alloc_init(&p, RSA_PRIME_LEN, pq, RSA_PRIME_LEN));
    CX_CHECK(cx_bn_alloc_init(&q, RSA_PRIME_LEN, pq + RSA_PRIME_LEN, RSA_PRIME_LEN));
    CX_CHECK(cx_bn_alloc(&gcd, RSA_PRIME_LEN));
    CX_CHECK(cx_bn_set_u32(gcd, 1));
    CX_CHECK(cx_bn_sub(p, p, gcd));
    CX_CHECK(cx_bn_sub(q, q, gcd));

    CX_CHECK(cx_bn_alloc(&remainder, RSA_MODULUS_LEN));
    CX_CHECK(cx_bn_mul(remainder, p, q));
    CX_CHECK(cx_bn_cmp_u32(q, 0, &compare));
    while (compare != 0) {
        CX_CHECK(cx_bn_reduce(gcd, p, q));
        const cx_bn_t previous_p = p;
        p = q;
        q = gcd;
        gcd = previous_p;
        CX_CHECK(cx_bn_cmp_u32(q, 0, &compare));
    }
    CX_CHECK(cx_bn_cnt_bits(p, &gcd_bits));

    // phi > 2^4095. With gcd < 2^2030, lambda > 2^2065, so
    // e*d >= lambda + 1 forces d > 2^2048 whenever the inverse exists.
    // RSA key generation below checks that the inverse exists.
    if (gcd_bits <= 2030) {
        goto end;
    }

    // For a larger gcd, divide phi by gcd and check the actual inverse.
    CX_CHECK(cx_bn_destroy(&q));
    CX_CHECK(cx_bn_destroy(&gcd));
    CX_CHECK(cx_bn_alloc(&divisor, RSA_MODULUS_LEN));
    CX_CHECK(cx_bn_copy(divisor, p));
    CX_CHECK(cx_bn_destroy(&p));
    CX_CHECK(cx_bn_alloc(&lambda, RSA_MODULUS_LEN));
    CX_CHECK(cx_bn_set_u32(lambda, 0));
    CX_CHECK(cx_bn_cnt_bits(remainder, &phi_bits));
    int32_t bit = (int32_t)(phi_bits - gcd_bits);
    CX_CHECK(cx_bn_shl(divisor, (uint32_t)bit));
    for (; bit >= 0; bit--) {
        CX_CHECK(cx_bn_cmp(remainder, divisor, &compare));
        if (compare >= 0) {
            CX_CHECK(cx_bn_sub(remainder, remainder, divisor));
            CX_CHECK(cx_bn_set_bit(lambda, (uint32_t)bit));
        }
        CX_CHECK(cx_bn_shr(divisor, 1));
    }
    CX_CHECK(cx_bn_mod_u32_invert(remainder, 65537, lambda));
    CX_CHECK(cx_bn_set_u32(divisor, 0));
    CX_CHECK(cx_bn_set_bit(divisor, 2048));
    CX_CHECK(cx_bn_cmp(remainder, divisor, &compare));
    if (compare <= 0) {
        error = CX_INVALID_PARAMETER;
    }

end:
    cx_bn_unlock();
    return error;
}
#endif

static void mark_slot_used(uint8_t slot) {
    uint64_t latest = 0;
    for (uint8_t i = 0; i < SLOT_COUNT; i++) {
        if (N_crypto_store[i].initialized && N_crypto_store[i].last_used > latest) {
            latest = N_crypto_store[i].last_used;
        }
    }
    SET_NV((void *)&N_crypto_store[slot].last_used, uint64_t, latest + 1)
}

static zxerr_t crypto_init_keys(const uint8_t *pq, uint8_t *seed_hash) {
    zemu_log_stack("crypto::initKeys");

    cx_rsa_4096_public_key_t rsa_pub;
    cx_rsa_4096_private_key_t rsa_priv;
    const uint8_t exp_be_buf[] = {0x00, 0x01, 0x00, 0x01};    // default rsa value 65537

    view_message_show(MENU_MAIN_APP_LINE1, "gen pair");
    UX_WAIT_DISPLAYED();

    zxerr_t error = zxerr_unknown;
#ifndef APP_TESTING
    if (!legacy_wallet) {
        CATCH_CXERROR(validate_standard_exponent(pq));
    }
#endif
    CATCH_CXERROR(cx_rsa_generate_pair_no_throw(RSA_MODULUS_LEN,
                                               (cx_rsa_public_key_t * ) & rsa_pub,
                                               (cx_rsa_private_key_t * ) & rsa_priv,
                                               exp_be_buf, 4,
                                               pq));
#ifndef APP_TESTING
    if (!legacy_wallet && (rsa_pub.n[0] & 0x80) == 0) {
        goto catch_cx_error;
    }
#endif

    view_message_show(MENU_MAIN_APP_LINE1, "store keys");
    UX_WAIT_DISPLAYED();
    MEMCPY_NV((void *)&N_crypto_store[slot_in_use].rsa_pub, &rsa_pub, sizeof(rsa_pub));
    MEMCPY_NV((void *)&N_crypto_store[slot_in_use].rsa_priv, &rsa_priv, sizeof(rsa_priv));
    MEMCPY_NV((void *)&N_crypto_store[slot_in_use].seed_hash, seed_hash, CX_SHA256_SIZE);
    SET_NV((void *)&N_crypto_store[slot_in_use].number, uint32_t, wallet_number)
    SET_NV((void *)&N_crypto_store[slot_in_use].legacy, bool, legacy_wallet)
    mark_slot_used(slot_in_use);
    SET_NV((void *)&N_crypto_store[slot_in_use].initialized, bool, true)
    error = zxerr_ok;

catch_cx_error:
    MEMZERO(&rsa_priv, sizeof(rsa_priv));
    return error;
}

static void clear_slot(uint8_t slot) {
    SET_NV((void *)&N_crypto_store[slot].initialized, uint8_t, false)
    nvm_write((void *)&N_crypto_store[slot], NULL, sizeof(N_crypto_store[slot]));
}

static bool select_cached_wallet(void) {
    uint8_t seed[64] = {0};
    uint8_t seed_hash[CX_SHA256_SIZE] = {0};
    const bool hash_ready = derive_master_seed(seed) == CX_OK &&
        cx_hash_sha256(seed, MASTERSEED_LEN, seed_hash, sizeof(seed_hash)) == sizeof(seed_hash);
    MEMZERO(seed, sizeof(seed));

    bool found = false;
    for (uint8_t slot = 0; hash_ready && slot < SLOT_COUNT; slot++) {
        if (crypto_store_slot_is_initialized(slot) &&
            N_crypto_store[slot].legacy == legacy_wallet && N_crypto_store[slot].number == wallet_number &&
            MEMCMP((const void *)&N_crypto_store[slot].seed_hash, seed_hash, sizeof(seed_hash)) == 0) {
            slot_in_use = slot;
            found = true;
            break;
        }
    }
    MEMZERO(seed_hash, sizeof(seed_hash));
    return found;
}

static zxerr_t crypto_initialize_slot(void) {
    uint8_t seed[64] = {0};
    uint8_t seed_hash[CX_SHA256_SIZE] = {0};
    uint8_t pq[RSA_PRIME_LEN * 2] = {0};
    zxerr_t result = zxerr_unknown;

    view_message_show(MENU_MAIN_APP_LINE1, "Initializing");
    UX_WAIT_DISPLAYED();
    if (derive_master_seed(seed) == CX_OK &&
        cx_hash_sha256(seed, MASTERSEED_LEN, seed_hash, sizeof(seed_hash)) == sizeof(seed_hash) &&
        crypto_init_primes(pq, seed) == zxerr_ok) {
        MEMZERO(seed, sizeof(seed));
        result = crypto_init_keys(pq, seed_hash);
    }
    MEMZERO(seed, sizeof(seed));
    MEMZERO(seed_hash, sizeof(seed_hash));
    MEMZERO(pq, sizeof(pq));
    return result;
}

zxerr_t crypto_store_init() {
    if (select_cached_wallet()) {
        device_initialized = true;
        return zxerr_ok;
    }

    slot_in_use = 0;
    for (uint8_t slot = 0; slot < SLOT_COUNT; slot++) {
        if (!crypto_store_slot_is_initialized(slot)) {
            slot_in_use = slot;
            break;
        }
        if (N_crypto_store[slot].last_used < N_crypto_store[slot_in_use].last_used) {
            slot_in_use = slot;
        }
    }
    if (crypto_store_slot_is_initialized(slot_in_use)) {
        clear_slot(slot_in_use);
    }
    const zxerr_t result = crypto_initialize_slot();
    device_initialized = result == zxerr_ok;
    return result;
}

uint32_t crypto_store_wallet_number(void) {
    return wallet_number;
}

void crypto_store_use_wallet_number(uint32_t number) {
    if (wallet_number == number) return;
    wallet_number = number;
    signature_set = 0;
    device_initialized = select_cached_wallet();
    if (device_initialized) {
        mark_slot_used(slot_in_use);
        view_idle_show(0, NULL);
    } else {
        view_initialize_show(0, NULL);
    }
}

const char *crypto_store_wallet_label(void) {
    return legacy_wallet ? "Legacy Arweave" : "Standard RSA-4096";
}

void crypto_store_switch_wallet(void) {
#ifdef APP_TESTING
    return;
#endif
    legacy_wallet = !legacy_wallet;
    signature_set = 0;
    reset_signing_session();
    device_initialized = select_cached_wallet();
    if (device_initialized) {
        mark_slot_used(slot_in_use);
        view_idle_show(0, NULL);
    } else {
        view_initialize_show(0, NULL);
    }
}

bool crypto_store_init_test() {
#ifdef APP_TESTING
    slot_in_use = KEY_SLOT_1;
    if (!crypto_store_slot_is_initialized(slot_in_use)) {
        uint8_t pq[RSA_PRIME_LEN * 2] = {0};
        for (size_t i = 0; i < sizeof(pq); i++) {
            const char high = TEST_RSA_PQ_HEX[i * 2];
            const char low = TEST_RSA_PQ_HEX[i * 2 + 1];
            const uint8_t high_nibble = high <= '9' ? high - '0' : high - 'a' + 10;
            const uint8_t low_nibble = low <= '9' ? low - '0' : low - 'a' + 10;
            pq[i] = (high_nibble << 4) | low_nibble;
        }
        uint8_t seed_hash[CX_SHA256_SIZE] = {0};
        const zxerr_t result = crypto_init_keys(pq, seed_hash);
        MEMZERO(pq, sizeof(pq));
        if (result != zxerr_ok) {
            return false;
        }
    }
    device_initialized = true;
    return true;
#endif

    return select_cached_wallet();
}

cx_rsa_4096_public_key_t *crypto_store_get_pubkey() {
    if (!crypto_store_is_initialized()) {
        return NULL;
    }
    return (cx_rsa_4096_public_key_t *) &N_crypto_store[slot_in_use].rsa_pub;
}

cx_rsa_4096_private_key_t *crypto_store_get_privkey() {
    if (!crypto_store_is_initialized()) {
        return NULL;
    }
    return (cx_rsa_4096_private_key_t *)&N_crypto_store[slot_in_use].rsa_priv;
}
