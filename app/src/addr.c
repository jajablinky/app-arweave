/*******************************************************************************
*   (c) 2020 Zondax GmbH
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

#include <stdio.h>
#include "coin.h"
#include "zxerror.h"
#include "zxmacros.h"
#include "zxformat.h"
#include "app_mode.h"
#include "crypto.h"
#include "crypto_store.h"
#include "addr.h"

zxerr_t addr_getNumItems(uint8_t *num_items) {
    *num_items = 2;
    return zxerr_ok;
}

zxerr_t addr_getItem(int8_t displayIdx,
                     char *outKey, uint16_t outKeyLen,
                     char *outVal, uint16_t outValLen,
                     uint8_t pageIdx, uint8_t *pageCount) {
    if (displayIdx == 0) {
        snprintf(outKey, outKeyLen, "Wallet %lu", (unsigned long)crypto_store_wallet_number());
        pageString(outVal, outValLen, crypto_store_wallet_label(), pageIdx, pageCount);
        return zxerr_ok;
    }
    if (displayIdx == 1) {
        uint8_t address[48];
        uint16_t length;
        CHECK_ZXERR(crypto_fillAddress(address, sizeof(address), &length));
        snprintf(outKey, outKeyLen, "Address");
        pageString(outVal, outValLen, (const char *)address, pageIdx, pageCount);
        return zxerr_ok;
    }
    return zxerr_no_data;
}
