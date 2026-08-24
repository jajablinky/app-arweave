# Permaweb Ledger app

This repository contains an experimental Permaweb application for Ledger
devices. It adds AO HTTP signature and ANS-104 Data Item signing to the
upstream Arweave application.

The firmware uses the on-device name `Permaweb`. It has a separate application
identity, so development builds can be installed alongside the official
Arweave app.

This project is not approved, endorsed, or distributed by Ledger. It has not
completed Ledger validation or an independent security audit. Do not use it on
a device that protects funded accounts.

## Current status

The current development targets are:

| Device | Build | Speculos tests | Offline installer package |
| --- | --- | --- | --- |
| Ledger Nano S Plus | Yes | Yes | Yes |
| Ledger Nano X | Yes | Yes | No |

The Nano S Plus package still requires a physical installation check on a
dedicated development device. There is no approved Ledger Live release.

## Signing support

The app retains upstream Arweave transaction signing and adds two experimental
commands:

- Canonical ANS-104 Data Item parsing and RSA-4096 signing
- AO mainnet HTTP signature-base parsing and RSA-PSS/SHA-512 signing

The device parses the complete payload, checks that it belongs to the device
key, and displays the committed fields before approval. It does not accept a
host-supplied digest for either experimental command.

Read the protocol and review limits before integrating either command:

- [ANS-104 Data Item signing](docs/ANS104.md)
- [AO HTTP Signature signing](docs/HTTPSIG.md)
- [APDU specification](docs/APDUSPEC.md)

## Safety rules

- Use a dedicated development Ledger initialized with a test seed.
- Never sideload this app onto a device that protects real funds.
- Treat every generated package as an unvetted development build.
- Do not describe the app as Ledger-approved or production-ready.

## Clone and initialize

Clone the repository with its pinned SDK and UI dependencies:

```sh
git clone --recurse-submodules https://github.com/jajablinky/app-arweave.git
cd app-arweave
```

If the repository is already cloned, initialize its submodules before building:

```sh
git submodule update --init --recursive
```

The current build targets run in Docker. Install Docker and GNU Make before
continuing.

## Build

Build the Nano S Plus firmware:

```sh
make current_build
```

Build the Nano X firmware:

```sh
make current_build_nanox
```

Build the Nano S Plus firmware and its offline loader command package:

```sh
make current_package
```

The package is written to:

```text
app/output/permaweb-nanos-plus-1.1.0.apdu
```

`current_package` applies the tracked Permaweb UI patch, builds against the
current Nano S Plus SDK, and emits an API-level-26 loader package for target
`0x33100004`. The package is a development artifact. It is not a Ledger Live
installer or an approved release.

See [Permaweb application packaging](PERMAWEB_APP.md) for the package identity
and UI patch details.

## Test

Run the native parser and cryptography tests:

```sh
make cpp_test
```

Build test firmware for Nano S Plus and Nano X, run the review flows in
Speculos, retrieve each signature, and verify it independently:

```sh
make current_ragger
```

Test firmware uses a disposable RSA key fixture. Production builds derive
their keys from the Ledger seed and do not include that fixture.

The physical-device test is documented in
[tests_ragger/README.md](tests_ragger/README.md). It verifies a signature but
does not broadcast data.

## License and attribution

The source is distributed under the
[Apache License 2.0](LICENSE). [NOTICE](NOTICE) retains the relevant Ledger and
Zondax copyright notices and identifies the Forward Research modifications.

This project is derived from
[LedgerHQ/app-arweave](https://github.com/LedgerHQ/app-arweave) and
[Zondax/ledger-arweave](https://github.com/Zondax/ledger-arweave). Product
branding is separate from that required source attribution.
