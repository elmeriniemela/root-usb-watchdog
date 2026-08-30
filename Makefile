CC ?= cc
CFLAGS ?= -O2
CPPFLAGS ?=
WARNINGS := -Wall -Wextra -Wpedantic -Werror
STATIC_LDFLAGS := -static -Wl,--build-id=none

PROGRAM := root-usb-watchdog
BUILD_DIR := build
DIST_DIR := dist
TEST_PROGRAM := $(BUILD_DIR)/test-resolver

.PHONY: all clean release static-check test unit-verify

all: $(BUILD_DIR)/$(PROGRAM)

$(BUILD_DIR):
	mkdir -p $@

$(DIST_DIR):
	mkdir -p $@

$(BUILD_DIR)/$(PROGRAM): root-usb-watchdog.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(STATIC_LDFLAGS) -o $@ $<

$(TEST_PROGRAM): tests/test_resolver.c root-usb-watchdog.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -Wno-unused-function -o $@ tests/test_resolver.c

test: $(BUILD_DIR)/$(PROGRAM) $(TEST_PROGRAM)
	$(TEST_PROGRAM)
	bash tests/test_cli.sh $(BUILD_DIR)/$(PROGRAM)
	$(MAKE) static-check
	$(MAKE) unit-verify

static-check: $(BUILD_DIR)/$(PROGRAM)
	@if ldd $(BUILD_DIR)/$(PROGRAM) 2>&1 | grep -vq 'not a dynamic executable'; then \
		echo "$(PROGRAM) is not fully static" >&2; \
		exit 1; \
	fi
	@echo "static linkage verified"

unit-verify: $(BUILD_DIR)/$(PROGRAM)
	rm -rf $(BUILD_DIR)/verify-root
	mkdir -p $(BUILD_DIR)/verify-root/etc/systemd/system
	mkdir -p $(BUILD_DIR)/verify-root/usr/local/sbin
	cp root-usb-watchdog.service $(BUILD_DIR)/verify-root/etc/systemd/system/
	cp tests/minimal.target $(BUILD_DIR)/verify-root/etc/systemd/system/
	cd $(BUILD_DIR)/verify-root/etc/systemd/system && \
		for target in basic graphical local-fs multi-user shutdown sysinit; do \
			cp minimal.target "$$target.target"; \
		done
	cp $(BUILD_DIR)/$(PROGRAM) $(BUILD_DIR)/verify-root/usr/local/sbin/
	systemd-analyze verify --root=$(BUILD_DIR)/verify-root root-usb-watchdog.service

release: test
	@test -n "$(VERSION)" || { echo "Use: make release VERSION=<version>" >&2; exit 1; }
	@test "$(VERSION)" = "0.1.0" || { echo "VERSION must match PROGRAM_VERSION (0.1.0)" >&2; exit 1; }
	mkdir -p $(DIST_DIR)
	cp $(BUILD_DIR)/$(PROGRAM) $(DIST_DIR)/$(PROGRAM)-$(VERSION)-x86_64-linux
	sha256sum $(DIST_DIR)/$(PROGRAM)-$(VERSION)-x86_64-linux > \
		$(DIST_DIR)/$(PROGRAM)-$(VERSION)-x86_64-linux.sha256

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR)
