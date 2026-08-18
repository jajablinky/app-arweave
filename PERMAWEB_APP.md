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
make current_package
```

The target applies the tracked `ledger-zxlib` UI patch idempotently, builds the firmware in Ledger's
container, and emits the 295-command loader package at
`app/output/permaweb-nanos-plus-1.1.0.apdu`.
