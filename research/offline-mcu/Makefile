CC ?= cc
PYTHON ?= python3
CPPFLAGS += -Iinclude -I.build
CFLAGS ?= -O2 -g
WARNINGS = -std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow
SOURCES = src/goodix_protocol.c tests/test_protocol.c
HEADERS = include/goodix_protocol.h .build/windows_fixtures.h

.PHONY: all test sanitize audit clean modules install
all: test

.build:
	mkdir -p $@

.build/windows_fixtures.h: fixtures/windows-a8.json tools/fixtures_header.py | .build
	$(PYTHON) tools/fixtures_header.py $< $@

.build/test_protocol: $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(SOURCES) -o $@

test: .build/test_protocol
	./.build/test_protocol

.build/test_sanitize: $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) -O1 -g $(WARNINGS) -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie $(SOURCES) -o $@

sanitize: .build/test_sanitize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./.build/test_sanitize

.build/goodix_protocol.o: src/goodix_protocol.c include/goodix_protocol.h | .build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -c $< -o $@

audit: .build/goodix_protocol.o
	$(PYTHON) tools/audit_offline.py .build/goodix_protocol.o

# There is deliberately no kernel module or installation path in v0.6.
modules install:
	@echo 'v0.6 is offline only; module build/installation is unavailable.'
	@false

clean:
	rm -rf .build
