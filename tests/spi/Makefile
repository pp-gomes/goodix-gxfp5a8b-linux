# SPDX-License-Identifier: GPL-2.0-only
CC ?= cc
CFLAGS ?= -O2 -g
SRC := ../../src/spi
WARNINGS := -std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow

.PHONY: test sanitize
test sanitize:
	@set -eu; binary=$$(mktemp); trap 'rm -f "$$binary"' EXIT; \
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -UNDEBUG -pthread -I$(SRC) \
		$(SANITIZERS) $(SRC)/gx_spi_transport.c test_transport.c -o "$$binary"; \
	$(TEST_ENV) "$$binary"

sanitize: SANITIZERS := -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie
sanitize: TEST_ENV := ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1

.PHONY: test-linux sanitize-linux
test: test-linux
sanitize: sanitize-linux

test-linux sanitize-linux:
	@set -eu; build=$$(mktemp -d); trap 'rm -rf "$$build"' EXIT; \
	for header in acpi.h completion.h mfd/core.h pci.h platform_device.h \
		property.h pxa2xx_ssp.h refcount.h slab.h spi/spi.h; do \
		mkdir -p "$$build/linux/$$(dirname "$$header")"; \
		printf '%s\n' '#include "mock_linux.h"' > "$$build/linux/$$header"; \
	done; \
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter-out -std=c11 -Wpedantic,$(WARNINGS)) -std=gnu11 \
		-UNDEBUG -pthread -I. -I$(SRC) -I"$$build" $(SANITIZERS) \
		$(SRC)/gx_spi_transport.c $(SRC)/gx_spi_linux.c $(SRC)/gx_spi_driver.c \
		test_linux.c -Wl,--wrap=gx_spi_linux_execute -o "$$build/test_linux"; \
	$(TEST_ENV) "$$build/test_linux"
