# Ragger tests

These tests always use the fixed public test seed in `conftest.py`. Never point
them at a physical Ledger that holds funds.

Build the firmware first, then run a supported model through Speculos:

```sh
python3 -m venv .venv-ragger
.venv-ragger/bin/pip install -r tests_ragger/requirements.txt
.venv-ragger/bin/pytest tests_ragger --device nanosp --backend speculos
```

The inherited app currently identifies as SDK API level 5, which current
Speculos does not support. The command becomes runnable after the planned SDK
migration. Speculos 0.10 can boot the legacy ELF, but the app's first-run RSA
initialization is not suitable for the current Ragger lifecycle.
