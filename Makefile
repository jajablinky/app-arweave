#*******************************************************************************
#*   (c) 2019 Zondax GmbH
#*   Modifications copyright 2026 Forward Research
#*
#*  Licensed under the Apache License, Version 2.0 (the "License");
#*  you may not use this file except in compliance with the License.
#*  You may obtain a copy of the License at
#*
#*      http://www.apache.org/licenses/LICENSE-2.0
#*
#*  Unless required by applicable law or agreed to in writing, software
#*  distributed under the License is distributed on an "AS IS" BASIS,
#*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#*  See the License for the specific language governing permissions and
#*  limitations under the License.
#********************************************************************************

# We use BOLOS_SDK to determine the development environment that is being used
# BOLOS_SDK IS  DEFINED	 	We use the plain Makefile for Ledger
# BOLOS_SDK NOT DEFINED		We use a containerized build approach

TESTS_JS_PACKAGE = "@zondax/ledger-arweave"
TESTS_JS_DIR = $(CURDIR)/js

LEDGER_BUILDER_IMAGE ?= ghcr.io/ledgerhq/ledger-app-builder/ledger-app-builder-lite:latest
LEDGER_DEV_TOOLS_IMAGE ?= ghcr.io/ledgerhq/ledger-app-builder/ledger-app-dev-tools:latest
LEDGER_LOADER_IMAGE ?= zondax/ledger-app-builder:ledger-e1420b8a420f5f3d503ce27bbe998831e6047d6c
GIT_COMMON_DIR := $(shell realpath "$$(git rev-parse --git-common-dir)")
GIT_COMMON_MOUNT := -v "$(GIT_COMMON_DIR):$(GIT_COMMON_DIR):ro"
PERMAWEB_PACKAGE := app/output/permaweb-nanos-plus-1.1.0.apdu

.DEFAULT_GOAL := current_build

.PHONY: customize current_package current_build current_build_test current_build_test_nanox current_build_nanox \
	current_ragger current_ragger_nanosp current_ragger_nanox
customize:
	@if git -C deps/ledger-zxlib apply --reverse --check ../../patches/permaweb-ledger-zxlib.patch >/dev/null 2>&1; then \
		echo "Permaweb Ledger UI patch is already applied"; \
	elif git -C deps/ledger-zxlib apply --check ../../patches/permaweb-ledger-zxlib.patch >/dev/null 2>&1; then \
		git -C deps/ledger-zxlib apply ../../patches/permaweb-ledger-zxlib.patch; \
	else \
		echo "Permaweb Ledger UI patch does not match the pinned ledger-zxlib source" >&2; \
		exit 1; \
	fi

current_package: customize current_build
	docker run --rm -e SDK_VARNAME=NANOSP_SDK -e TARGET=nanos2 -u $$(id -u):$$(id -g) \
		-v "$(CURDIR):/app" $(LEDGER_LOADER_IMAGE) \
		"python3 -m ledgerblue.loadApp --targetId 0x33100004 --apiLevel 26 \
		--fileName /app/app/bin/app.hex --appName Permaweb --appFlags 0x000 --delete --tlv \
		--dataSize 22016 --installparamsSize 73 --path 44'/472' --path 44'/1' \
		--offline /app/$(PERMAWEB_PACKAGE) --offlineText"
	@shasum -a 256 $(PERMAWEB_PACKAGE)
	@wc -l $(PERMAWEB_PACKAGE)

current_build:
	docker run --rm -v "$(CURDIR):/app" $(GIT_COMMON_MOUNT) -w /app $(LEDGER_BUILDER_IMAGE) \
		bash -lc 'make -C app clean BOLOS_SDK="$$NANOSP_SDK" && \
		make -C app -j$$(nproc) BOLOS_SDK="$$NANOSP_SDK"'

current_build_test:
	docker run --rm -v "$(CURDIR):/app" $(GIT_COMMON_MOUNT) -w /app $(LEDGER_BUILDER_IMAGE) \
		bash -lc 'make -C app clean BOLOS_SDK="$$NANOSP_SDK" && \
		make -C app -j$$(nproc) BOLOS_SDK="$$NANOSP_SDK" APP_TESTING=1'

current_build_test_nanox:
	docker run --rm -v "$(CURDIR):/app" $(GIT_COMMON_MOUNT) -w /app $(LEDGER_BUILDER_IMAGE) \
		bash -lc 'make -C app clean BOLOS_SDK="$$NANOX_SDK" && \
		make -C app -j$$(nproc) BOLOS_SDK="$$NANOX_SDK" APP_TESTING=1'

current_build_nanox:
	docker run --rm -v "$(CURDIR):/app" $(GIT_COMMON_MOUNT) -w /app $(LEDGER_BUILDER_IMAGE) \
		bash -lc 'make -C app clean BOLOS_SDK="$$NANOX_SDK" && \
		make -C app -j$$(nproc) BOLOS_SDK="$$NANOX_SDK"'

current_ragger: current_ragger_nanosp current_ragger_nanox

current_ragger_nanosp: current_build_test
	docker run --rm -v "$(CURDIR):/app" $(GIT_COMMON_MOUNT) -w /app/app $(LEDGER_DEV_TOOLS_IMAGE) \
		bash -lc 'python -m venv /tmp/ragger && \
		/tmp/ragger/bin/pip install --quiet -r ../tests_ragger/requirements.txt && \
		/tmp/ragger/bin/pytest ../tests_ragger -v --tb=short --device nanosp --backend speculos'

current_ragger_nanox: current_build_test_nanox
	docker run --rm -v "$(CURDIR):/app" $(GIT_COMMON_MOUNT) -w /app/app $(LEDGER_DEV_TOOLS_IMAGE) \
		bash -lc 'python -m venv /tmp/ragger && \
		/tmp/ragger/bin/pip install --quiet -r ../tests_ragger/requirements.txt && \
		/tmp/ragger/bin/pytest ../tests_ragger -v --tb=short --device nanox --backend speculos'

ifeq ($(BOLOS_SDK),)
include $(CURDIR)/deps/ledger-zxlib/dockerized_build.mk
else
default:
	$(MAKE) -C app
%:
	$(info "Calling app Makefile for target $@")
	COIN=$(COIN) $(MAKE) -C app $@
endif

test_all:
	make zemu_install

	# test addresses generation
	make
	COIN=addr make zemu_test

	# test workflows
	APP_TESTING=1 make
	make zemu_test
