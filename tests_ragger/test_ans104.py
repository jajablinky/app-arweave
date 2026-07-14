# Modifications copyright 2026 Forward Research. Apache-2.0.

from struct import pack
from time import sleep, time

import pytest
from ragger.backend import BackendInterface
from ragger.error import ExceptionRAPDU
from ragger.navigator import NavInsID, Navigator

CLA = 0x44
INS_SIGN_DATA_ITEM = 0x03
INS_GET_PK = 0x20

TAGS = bytes.fromhex(
    "0c1a446174612d50726f746f636f6c04616f0e56617269616e740e616f2e544e2e31"
    "08547970650e4d6573736167650c416374696f6e105472616e73666572125265636970"
    "69656e7422726563697069656e742d61646472657373105175616e7469747908313030"
    "3000"
)


def wait_until_initialized(backend: BackendInterface) -> None:
    deadline = time() + 60
    while time() < deadline:
        try:
            response = backend.exchange(CLA, INS_GET_PK, 0, 0, b"")
            if response.status == 0x9000:
                return
        except ExceptionRAPDU as error:
            if error.status != 0x6987:
                raise
        sleep(0.25)
    raise TimeoutError("Arweave key generation did not finish")


def get_owner(backend: BackendInterface) -> bytes:
    first = backend.exchange(CLA, INS_GET_PK, 0, 0, b"")
    second = backend.exchange(CLA, INS_GET_PK, 0, 1, b"")
    assert first.status == 0x9000
    assert second.status == 0x9000
    return first.data + second.data


def data_item(owner: bytes) -> bytes:
    assert len(owner) == 512
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


def initialize_and_add(backend: BackendInterface, payload: bytes) -> bytes:
    response = backend.exchange(CLA, INS_SIGN_DATA_ITEM, 0, 0, b"")
    assert response.status == 0x9000
    chunks = [payload[i : i + 250] for i in range(0, len(payload), 250)]
    for chunk in chunks[:-1]:
        response = backend.exchange(CLA, INS_SIGN_DATA_ITEM, 1, 0, chunk)
        assert response.status == 0x9000
    return chunks[-1]


def test_rejects_non_arweave_signature_type(backend: BackendInterface):
    wait_until_initialized(backend)
    payload = bytearray(data_item(bytes(512)))
    payload[0] = 2
    final_chunk = initialize_and_add(backend, payload)
    with pytest.raises(ExceptionRAPDU) as error:
        backend.exchange(CLA, INS_SIGN_DATA_ITEM, 2, 0, final_chunk)
    assert error.value.status == 0x6984


def test_reviews_and_approves_ao_message(
    backend: BackendInterface, navigator: Navigator, test_name: str
):
    wait_until_initialized(backend)
    final_chunk = initialize_and_add(backend, data_item(get_owner(backend)))
    with backend.exchange_async(CLA, INS_SIGN_DATA_ITEM, 2, 0, final_chunk):
        navigator.navigate_until_text_and_compare(
            NavInsID.RIGHT_CLICK,
            [NavInsID.BOTH_CLICK],
            "Approve",
            test_name,
        )
    assert backend.last_async_response is not None
    assert backend.last_async_response.status == 0x9000
    assert len(backend.last_async_response.data) == 48
