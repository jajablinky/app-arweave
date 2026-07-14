#!/usr/bin/env python3
"""Exercise ANS-104 signing on a connected development Ledger."""

from base64 import urlsafe_b64encode
from hashlib import sha256
from struct import pack

from Crypto.Hash import SHA256
from Crypto.PublicKey import RSA
from Crypto.Signature import pss
from ledgerblue.comm import getDongle

CLA = 0x44
INS_SIGN_DATA_ITEM = 0x03
INS_GET_SIG = 0x10
INS_GET_PK = 0x20

TAGS = bytes.fromhex(
    "0c1a446174612d50726f746f636f6c04616f0e56617269616e740e616f2e544e2e31"
    "08547970650e4d6573736167650c416374696f6e105472616e73666572125265636970"
    "69656e7422726563697069656e742d61646472657373105175616e7469747908313030"
    "3000"
)


def exchange(dongle, instruction, p1=0, p2=0, data=b""):
    if len(data) > 255:
        raise ValueError("APDU payload exceeds 255 bytes")
    apdu = bytes([CLA, instruction, p1, p2, len(data)]) + data
    return dongle.exchange(apdu)


def get_owner(dongle):
    return exchange(dongle, INS_GET_PK, p2=0) + exchange(
        dongle, INS_GET_PK, p2=1
    )


def make_data_item(owner):
    return b"".join(
        [
            b"\x01\x00",
            bytes(512),
            owner,
            b"\x01",
            bytes([0x22]) * 32,
            b"\x00",
            pack("<Q", 6),
            pack("<Q", len(TAGS)),
            TAGS,
            b"hello ao",
        ]
    )


def main():
    dongle = getDongle(False)
    try:
        owner = get_owner(dongle)
        if len(owner) != 512:
            raise ValueError(f"Unexpected owner length: {len(owner)}")

        address = urlsafe_b64encode(sha256(owner).digest()).rstrip(b"=").decode()
        print(f"Connected Arweave address: {address}")

        payload = make_data_item(owner)
        exchange(dongle, INS_SIGN_DATA_ITEM)
        chunks = [payload[i : i + 250] for i in range(0, len(payload), 250)]
        for chunk in chunks[:-1]:
            exchange(dongle, INS_SIGN_DATA_ITEM, p1=1, data=chunk)

        print("Review the ANS-104 message on the Ledger, then approve or reject it.")
        deep_hash = exchange(dongle, INS_SIGN_DATA_ITEM, p1=2, data=chunks[-1])
        if len(deep_hash) != 48:
            raise ValueError(f"Unexpected deep-hash length: {len(deep_hash)}")

        signature = exchange(dongle, INS_GET_SIG, p2=0) + exchange(
            dongle, INS_GET_SIG, p2=1
        )
        if len(signature) != 512:
            raise ValueError(f"Unexpected signature length: {len(signature)}")

        public_key = RSA.construct((int.from_bytes(owner, "big"), 65537))
        pss.new(public_key, salt_bytes=32).verify(SHA256.new(deep_hash), signature)
        print("PASS: the physical Ledger signature is valid. Nothing was broadcast.")
    finally:
        dongle.close()


if __name__ == "__main__":
    main()
