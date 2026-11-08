VERSION ?= 0.1.1
CC ?= cc
PKG_CONFIG ?= pkg-config
PYTHON ?= python3
PREFIX ?= /usr
DESTDIR ?=
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share/p2browser-probe
SOURCE_DATE_EPOCH ?= 1794096000
export SOURCE_DATE_EPOCH

# This release targets native amd64 Debian 13, with GTK 3 / WebKitGTK API 4.1.
WEBKIT_PKG := webkit2gtk-4.1
CFLAGS ?= -Os -g1
CPPFLAGS += -DP2_DATA_DIR=\"$(DATADIR)\" -DP2_VERSION=\"$(VERSION)\"
WARNINGS := -Wall -Wextra -Wformat=2 -Wshadow -Wstrict-prototypes
WEBKIT_CFLAGS = $(shell $(PKG_CONFIG) --cflags gtk+-3.0 $(WEBKIT_PKG))
WEBKIT_LIBS = $(shell $(PKG_CONFIG) --libs gtk+-3.0 $(WEBKIT_PKG))

.PHONY: all check-deps FORCE test smoke-test deb diagnostics-deb source install clean

all: build/p2browser-probe

check-deps:
	@$(PKG_CONFIG) --exists 'gtk+-3.0 >= 3.24' '$(WEBKIT_PKG) >= 2.40' || { printf '%s\n' 'Missing GTK 3 / WebKitGTK 4.1 development files. Install libgtk-3-dev and libwebkit2gtk-4.1-dev on Debian 13.' >&2; exit 1; }

# Rebuild the one translation unit so compiler/flag changes never reuse an old ELF.
build/p2browser-probe: src/main.c FORCE | check-deps
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c99 $(WEBKIT_CFLAGS) $(LDFLAGS) -o $@ $< $(WEBKIT_LIBS) $(LDLIBS)

FORCE:

test:
	$(PYTHON) -m unittest discover -s tests -p 'test_*.py' -v

# A working display, session bus and WebKit sandbox are required.
smoke-test: all
	@set -eu; \
	base=$$(mktemp -d "$(CURDIR)/build/smoke.XXXXXX"); \
	printf 'Smoke-test report: %s\n' "$$base/report"; \
	dbus-run-session -- xvfb-run -a "$(CURDIR)/build/p2browser-probe" \
	  --data-dir "$(CURDIR)/fixtures" --report-dir "$$base/report" \
	  --self-test --quit-after 30

deb: all
	VERSION='$(VERSION)' sh packaging/build-deb.sh all

diagnostics-deb:
	VERSION='$(VERSION)' sh packaging/build-deb.sh diagnostics

source:
	VERSION='$(VERSION)' sh packaging/build-deb.sh source

install: all
	install -d -m 755 '$(DESTDIR)$(BINDIR)' '$(DESTDIR)$(DATADIR)' '$(DESTDIR)$(PREFIX)/share/applications'
	install -m 755 build/p2browser-probe '$(DESTDIR)$(BINDIR)/p2browser-probe'
	install -m 755 tools/collect-report.sh '$(DESTDIR)$(BINDIR)/p2browser-collect-report'
	install -m 755 tools/monitor.sh '$(DESTDIR)$(BINDIR)/p2browser-monitor'
	install -m 644 fixtures/start.html '$(DESTDIR)$(DATADIR)/start.html'
	install -m 644 packaging/p2browser-probe.desktop '$(DESTDIR)$(PREFIX)/share/applications/p2browser-probe.desktop'

clean:
	rm -rf build
