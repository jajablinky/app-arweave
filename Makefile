#*******************************************************************************
#*   (c) 2019 Zondax GmbH
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

.DEFAULT_GOAL := current_build

.PHONY: current_build current_build_test current_build_test_nanox current_build_nanox \
	current_ragger current_ragger_nanosp current_ragger_nanox
current_build:
	docker run --rm -v "$(CURDIR):/app" -w /app $(LEDGER_BUILDER_IMAGE) \
		bash -lc 'make -C app clean BOLOS_SDK="$$NANOSP_SDK" && \
		make -C app -j$$(nproc) BOLOS_SDK="$$NANOSP_SDK"'

current_build_test:
	docker run --rm -v "$(CURDIR):/app" -w /app $(LEDGER_BUILDER_IMAGE) \
		bash -lc 'make -C app clean BOLOS_SDK="$$NANOSP_SDK" && \
		make -C app -j$$(nproc) BOLOS_SDK="$$NANOSP_SDK" APP_TESTING=1'

current_build_test_nanox:
	docker run --rm -v "$(CURDIR):/app" -w /app $(LEDGER_BUILDER_IMAGE) \
		bash -lc 'make -C app clean BOLOS_SDK="$$NANOX_SDK" && \
		make -C app -j$$(nproc) BOLOS_SDK="$$NANOX_SDK" APP_TESTING=1'

current_build_nanox:
	docker run --rm -v "$(CURDIR):/app" -w /app $(LEDGER_BUILDER_IMAGE) \
		bash -lc 'make -C app clean BOLOS_SDK="$$NANOX_SDK" && \
		make -C app -j$$(nproc) BOLOS_SDK="$$NANOX_SDK"'

current_ragger: current_ragger_nanosp current_ragger_nanox

current_ragger_nanosp: current_build_test
	docker run --rm -v "$(CURDIR):/app" -w /app/app $(LEDGER_DEV_TOOLS_IMAGE) \
		bash -lc 'python -m venv /tmp/ragger && \
		/tmp/ragger/bin/pip install --quiet -r ../tests_ragger/requirements.txt && \
		/tmp/ragger/bin/pytest ../tests_ragger -v --tb=short --device nanosp --backend speculos'

current_ragger_nanox: current_build_test_nanox
	docker run --rm -v "$(CURDIR):/app" -w /app/app $(LEDGER_DEV_TOOLS_IMAGE) \
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
