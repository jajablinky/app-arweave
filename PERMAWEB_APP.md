# Permaweb Ledger app

This branch builds the experimental AO-capable firmware as a separate Ledger application named
`Permaweb`. It does not use the `Arweave` application identity, so it can be installed alongside the
official Arweave app.

The Nano S Plus menu keeps the version, Apache 2.0 license, and quit screens, but omits the upstream
`Developed by` screen. Source copyright and license notices remain intact. Its icon keeps the current
Arweave ring and replaces the inner `a` glyph with a pixel-matched `P`.

Redistributions of the source or installer package must include the repository's unmodified Apache
2.0 `LICENSE` and its `NOTICE`, which preserves upstream attribution and identifies Forward Research's
modifications. Attribution is distributed with the software rather than presented as product branding
on the Ledger screen.

The pinned `ledger-zxlib` dependency owns the menu implementation. After initializing submodules,
build the complete Nano S Plus loader package with:

```sh
make current_package NANOSP_API26_SDK=/absolute/path/to/ledger-secure-sdk-v26.6.0
```

The target applies the tracked `ledger-zxlib` UI patch idempotently, builds the firmware in Ledger's
container, and copies the SDK-generated API 26 loader package to
`app/output/permaweb-nanos-plus-1.1.3.apdu`.

**Standard RSA-4096** is the default mode. **Wallet (switch)** opens **Legacy Arweave** so existing
addresses remain available. Compare the address on the Ledger before reconnecting an existing wallet.

For clients using numbered wallets, send `44 05 00 00 04` plus the wallet number as four big-endian
bytes. The range is 0 through 2³¹−1. The selected number lasts for the app session and applies to
address, owner and signing commands. Wallet 0 is selected at launch. A new wallet requires on-device
initialization. Its number, form and address appear on the device after initialization and during
signature review.
