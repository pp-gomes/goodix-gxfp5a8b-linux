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
