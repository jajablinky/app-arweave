/*******************************************************************************
*   (c) 2018, 2019 Zondax GmbH
*   (c) 2016 Ledger
*   Modifications (c) 2026 Forward Research
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

#include "app_main.h"

#include <string.h>
#include <os_io_seproxyhal.h>
#include <os.h>
#include <ux.h>

#include "view.h"
#include "actions.h"
#include "tx.h"
#include "addr.h"
#include "crypto.h"
#include "crypto_store.h"
#include "coin.h"
#include "zxmacros.h"

#include "view_internal.h"

static bool tx_initialized = false;
static uint8_t tx_instruction = 0;

extern bool device_initialized;

void reset_signing_session(void) {
    tx_initialized = false;
    tx_reset();
}

static void select_wallet(uint32_t rx) {
    if (rx != OFFSET_DATA + 4 || G_io_apdu_buffer[OFFSET_DATA_LEN] != 4) {
        THROW(APDU_CODE_WRONG_LENGTH);
    }
    if (G_io_apdu_buffer[OFFSET_P1] != 0 || G_io_apdu_buffer[OFFSET_P2] != 0 ||
        (G_io_apdu_buffer[OFFSET_DATA] & 0x80) != 0) {
        THROW(APDU_CODE_INVALIDP1P2);
    }
    const uint8_t *data = G_io_apdu_buffer + OFFSET_DATA;
    const uint32_t number = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                            ((uint32_t)data[2] << 8) | data[3];
    reset_signing_session();
    crypto_store_use_wallet_number(number);
    THROW(APDU_CODE_OK);
}

static bool process_chunk(volatile uint32_t *tx, uint32_t rx) {
    UNUSED(tx);
    const uint8_t payloadType = G_io_apdu_buffer[OFFSET_PAYLOAD_TYPE];

    if (rx < OFFSET_DATA) {
        THROW(APDU_CODE_WRONG_LENGTH);
    }

    if (G_io_apdu_buffer[OFFSET_P2] != 0) {
        tx_initialized = false;
        THROW(APDU_CODE_INVALIDP1P2);
    }

    const uint8_t instruction = G_io_apdu_buffer[OFFSET_INS];
    uint32_t added;
    switch (payloadType) {
        case P1_INIT:
            tx_initialize();
            tx_reset();
            tx_initialized = true;
            tx_instruction = instruction;
            return false;
        case P1_ADD:
            if (!tx_initialized) {
                THROW(APDU_CODE_TX_NOT_INITIALIZED);
            }
            if (tx_instruction != instruction) {
                tx_initialized = false;
                THROW(APDU_CODE_INVALIDP1P2);
            }
            added = tx_append(&(G_io_apdu_buffer[OFFSET_DATA]), rx - OFFSET_DATA);
            if (added != rx - OFFSET_DATA) {
                tx_initialized = false;
                THROW(APDU_CODE_OUTPUT_BUFFER_TOO_SMALL);
            }
            return false;
        case P1_LAST:
            if (!tx_initialized) {
                THROW(APDU_CODE_TX_NOT_INITIALIZED);
            }
            if (tx_instruction != instruction) {
                tx_initialized = false;
                THROW(APDU_CODE_INVALIDP1P2);
            }
            added = tx_append(&(G_io_apdu_buffer[OFFSET_DATA]), rx - OFFSET_DATA);
            tx_initialized = false;
            if (added != rx - OFFSET_DATA) {
                THROW(APDU_CODE_OUTPUT_BUFFER_TOO_SMALL);
            }
            return true;
    }
    tx_initialized = false;
    THROW(APDU_CODE_INVALIDP1P2);
}

__Z_INLINE void handle_getversion(volatile uint32_t *flags, volatile uint32_t *tx, uint32_t rx) {
    UNUSED(flags);
    UNUSED(rx);
#ifdef DEBUG
    G_io_apdu_buffer[0] = 0xFF;
#else
    G_io_apdu_buffer[0] = 0;
#endif
    G_io_apdu_buffer[1] = LEDGER_MAJOR_VERSION;
    G_io_apdu_buffer[2] = LEDGER_MINOR_VERSION;
    G_io_apdu_buffer[3] = LEDGER_PATCH_VERSION;
    G_io_apdu_buffer[4] = !IS_UX_ALLOWED;

    G_io_apdu_buffer[5] = (TARGET_ID >> 24) & 0xFF;
    G_io_apdu_buffer[6] = (TARGET_ID >> 16) & 0xFF;
    G_io_apdu_buffer[7] = (TARGET_ID >> 8) & 0xFF;
    G_io_apdu_buffer[8] = (TARGET_ID >> 0) & 0xFF;

    *tx += 9;
    THROW(APDU_CODE_OK);
}

__Z_INLINE void handleGetAddress(volatile uint32_t *flags, volatile uint32_t *tx, __Z_UNUSED uint32_t rx) {
    *tx = 0;
    if(G_io_apdu_buffer[OFFSET_DATA_LEN] != 0){
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }

    uint8_t requireConfirmation = G_io_apdu_buffer[OFFSET_P1];
    MEMZERO(G_io_apdu_buffer,IO_APDU_BUFFER_SIZE);

    action_addrResponseLen = 0;
    zxerr_t err = app_fill_address();
    if (err != zxerr_ok || action_addrResponseLen == 0) {
        *tx = 0;
        THROW(APDU_CODE_EXECUTION_ERROR);
    }

    if (requireConfirmation) {
        view_review_init(addr_getItem, addr_getNumItems, app_reply_address);
        view_review_show(REVIEW_ADDRESS);

        *flags |= IO_ASYNCH_REPLY;
        return;
    }else{
        *tx = action_addrResponseLen;
        THROW(APDU_CODE_OK);
    }
}

__Z_INLINE void handleGetPubKeyPart(__Z_UNUSED volatile uint32_t *flags, volatile uint32_t *tx, __Z_UNUSED uint32_t rx) {
    *tx = 0;
    if(G_io_apdu_buffer[OFFSET_DATA_LEN] != 0){
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }
    uint8_t index = G_io_apdu_buffer[OFFSET_P2];

    MEMZERO(G_io_apdu_buffer,IO_APDU_BUFFER_SIZE);
    zxerr_t err = crypto_getpubkey_part(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 3, index);
    if (err != zxerr_ok){
        *tx = 0;
        THROW(APDU_CODE_CONDITIONS_NOT_SATISFIED);
    }else{
        *tx = 256;
        THROW(APDU_CODE_OK);
    }
}


__Z_INLINE void handleGetSigPart(__Z_UNUSED volatile uint32_t *flags, volatile uint32_t *tx, __Z_UNUSED uint32_t rx) {
    *tx = 0;
    if(G_io_apdu_buffer[OFFSET_DATA_LEN] != 0){
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }

    uint8_t index = G_io_apdu_buffer[OFFSET_P2];

    MEMZERO(G_io_apdu_buffer,IO_APDU_BUFFER_SIZE);
    zxerr_t err = crypto_getsignature_part(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 3, index);
    if (err != zxerr_ok){
        *tx = 0;
        THROW(APDU_CODE_CONDITIONS_NOT_SATISFIED);
    }else{
        *tx = 256;
        THROW(APDU_CODE_OK);
    }
}


static viewfunc_getItem_t transaction_item;
static viewfunc_getNumItems_t transaction_item_count;

static zxerr_t review_item_count(uint8_t *count) {
    CHECK_ZXERR(transaction_item_count(count));
    *count += 2;
    return zxerr_ok;
}

static zxerr_t review_item(int8_t index, char *key, uint16_t key_len, char *value,
                          uint16_t value_len, uint8_t page, uint8_t *page_count) {
    if (index < 2) {
        return addr_getItem(index, key, key_len, value, value_len, page, page_count);
    }
    return transaction_item(index - 2, key, key_len, value, value_len, page, page_count);
}

static void review_signature(viewfunc_getItem_t item, viewfunc_getNumItems_t count,
                             viewfunc_accept_t accept) {
    transaction_item = item;
    transaction_item_count = count;
    view_review_init(review_item, review_item_count, accept);
    view_review_show(REVIEW_TXN);
}

__Z_INLINE void handleSign(volatile uint32_t *flags, volatile uint32_t *tx, uint32_t rx) {
    // Wait until all chunks are processed
    if (!process_chunk(tx, rx)) {
        THROW(APDU_CODE_OK);
    }

    zemu_log_stack("handleSign");

    const char *error_msg = tx_parse();
    if (error_msg != NULL) {
        int error_msg_length = strlen(error_msg);
        MEMCPY(G_io_apdu_buffer, error_msg, error_msg_length);
        *tx += (error_msg_length);
        THROW(APDU_CODE_DATA_INVALID);
    }

    CHECK_APP_CANARY()
    review_signature(tx_getItem, tx_getNumItems, app_sign);
    *flags |= IO_ASYNCH_REPLY;
}

__Z_INLINE void handleSignDataItem(volatile uint32_t *flags, volatile uint32_t *tx, uint32_t rx) {
    if (!process_chunk(tx, rx)) THROW(APDU_CODE_OK);

    const char *error_msg = dataitem_tx_parse();
    if (error_msg != NULL) {
        int error_msg_length = strlen(error_msg);
        MEMCPY(G_io_apdu_buffer, error_msg, error_msg_length);
        *tx += error_msg_length;
        THROW(APDU_CODE_DATA_INVALID);
    }

    CHECK_APP_CANARY()
    review_signature(dataitem_tx_getItem, dataitem_tx_getNumItems, app_sign_dataitem);
    *flags |= IO_ASYNCH_REPLY;
}

__Z_INLINE void handleSignHttp(volatile uint32_t *flags, volatile uint32_t *tx, uint32_t rx) {
    if (!process_chunk(tx, rx)) THROW(APDU_CODE_OK);
    const char *error_msg = httpsig_tx_parse();
    if (error_msg != NULL) {
        int error_msg_length = strlen(error_msg);
        MEMCPY(G_io_apdu_buffer, error_msg, error_msg_length);
        *tx += error_msg_length;
        THROW(APDU_CODE_DATA_INVALID);
    }
    CHECK_APP_CANARY()
    review_signature(httpsig_tx_getItem, httpsig_tx_getNumItems, app_sign_httpsig);
    *flags |= IO_ASYNCH_REPLY;
}

void handleApdu(volatile uint32_t *flags, volatile uint32_t *tx, uint32_t rx) {
    uint16_t sw = 0;

    BEGIN_TRY
    {
        TRY
        {
            if (G_io_apdu_buffer[OFFSET_CLA] != CLA) {
                THROW(APDU_CODE_CLA_NOT_SUPPORTED);
            }

            if (rx < APDU_MIN_LENGTH) {
                THROW(APDU_CODE_WRONG_LENGTH);
            }

            const uint8_t ins = G_io_apdu_buffer[OFFSET_INS];
            if (!device_initialized && ins != INS_GET_VERSION && ins != INS_SELECT_WALLET) {
                THROW(APDU_CODE_TX_NOT_INITIALIZED);
            }

            switch (ins) {
                case INS_GET_VERSION: {
                    handle_getversion(flags, tx, rx);
                    break;
                }

                case INS_SELECT_WALLET: {
                    CHECK_PIN_VALIDATED()
                    select_wallet(rx);
                    break;
                }

                case INS_GET_ADDR: {
                    CHECK_PIN_VALIDATED()
                    handleGetAddress(flags, tx, rx);
                    break;
                }

                case INS_SIGN: {
                    CHECK_PIN_VALIDATED()
                    handleSign(flags, tx, rx);
                    break;
                }

                case INS_SIGN_DATA_ITEM: {
                    CHECK_PIN_VALIDATED()
                    handleSignDataItem(flags, tx, rx);
                    break;
                }

                case INS_SIGN_HTTP: {
                    CHECK_PIN_VALIDATED()
                    handleSignHttp(flags, tx, rx);
                    break;
                }

                case INS_GET_SIG: {
                    CHECK_PIN_VALIDATED()
                    handleGetSigPart(flags, tx, rx);
                    break;
                }

                case INS_GET_PK: {
                    CHECK_PIN_VALIDATED()
                    handleGetPubKeyPart(flags, tx, rx);
                    break;
                }

                default:
                    THROW(APDU_CODE_INS_NOT_SUPPORTED);
            }
        }
        CATCH(EXCEPTION_IO_RESET)
        {
            THROW(EXCEPTION_IO_RESET);
        }
        CATCH_OTHER(e)
        {
            switch (e & 0xF000) {
                case 0x6000:
                case APDU_CODE_OK:
                    sw = e;
                    break;
                default:
                    sw = 0x6800 | (e & 0x7FF);
                    break;
            }
            G_io_apdu_buffer[*tx] = sw >> 8;
            G_io_apdu_buffer[*tx + 1] = sw & 0xFF;
            *tx += 2;
        }
        FINALLY
        {
        }
    }
    END_TRY;
}
