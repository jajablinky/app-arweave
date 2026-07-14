# Ragger tests

These tests always use the fixed public test seed in `conftest.py`. Never point
them at a physical Ledger that holds funds.

Build the API-level-26 firmware and run it through Speculos with the current
Ledger development images:

```sh
make current_ragger
```

Test builds preload a disposable RSA key so Speculos does not spend several
minutes generating one during every run. Production builds do not include that
fixture and continue to derive the key from the device seed.
