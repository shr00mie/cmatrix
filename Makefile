# cmatrix — Clang + Make build (Linux and macOS).
#
# Notcurses (required): use pkg-config as documented upstream:
#   https://github.com/dankamongmen/notcurses/blob/master/USAGE.md
#   — compile with: pkg-config --cflags notcurses
#   — link with:   pkg-config --libs notcurses
#
# Toolchain: default CC is clang (override: make CC=gcc). On Ubuntu, package
#   `clang` is recommended; see:
#   https://documentation.ubuntu.com/ubuntu-for-developers/reference/availability/llvm/
# macOS: default CC is `xcrun clang` (Apple Clang via the active Xcode/CLT developer
#   directory). That wrapper supplies -isysroot to the macOS SDK; the bare binary from
#   `xcrun -f clang` does not and fails to find <stdio.h>. Unlike first `clang` on PATH
#   (e.g. Homebrew LLVM). For Homebrew deps run: eval "$(brew shellenv)"
#
# C standard: C17 (-std=c17) is a reasonable portable baseline (ISO/IEC 9899:2018).
# Warnings: -Wall -Wextra are standard for catching obvious issues.
# Optimization: -O2 is a typical release default (not copied from legacy CMake).
#
# Generated: config.mk (HAVE_* probes). Run ./configure.sh first, or build
#   will run it automatically via the config.mk rule.

.PHONY: all clean install uninstall distclean configure

DESTDIR ?=

UNAME_S := $(shell uname -s)
# Linux and macOS: user-local install by default (~/.local/bin, no sudo). Override PREFIX for system-wide.
ifeq ($(UNAME_S),Darwin)
  PREFIX ?= $(HOME)/.local
else
  ifeq ($(UNAME_S),Linux)
    PREFIX ?= $(HOME)/.local
  else
    PREFIX ?= /usr/local
  endif
endif

ifeq ($(UNAME_S),Linux)
  # Debian/Ubuntu-style hardening often pairs _FORTIFY_SOURCE with -O; see distro docs.
  CFLAGS_OS ?= -O2 -pipe -fstack-protector-strong -D_FORTIFY_SOURCE=2
endif
ifeq ($(UNAME_S),Darwin)
  # Apple Clang: keep flags minimal beyond Notcurses pkg-config; avoid Linux-only _FORTIFY_SOURCE assumptions.
  CFLAGS_OS ?= -O2 -pipe
endif
ifndef CFLAGS_OS
  CFLAGS_OS ?= -O2 -pipe
endif

WARNFLAGS ?= -Wall -Wextra
STD ?= -std=c17

PKG_NOTCURSES_CFLAGS := $(shell pkg-config --cflags notcurses 2>/dev/null)
PKG_NOTCURSES_LIBS := $(shell pkg-config --libs notcurses 2>/dev/null)

ifeq ($(PKG_NOTCURSES_LIBS),)
$(error notcurses not found for pkg-config. Ubuntu: libnotcurses-dev; macOS: brew install notcurses and eval "$$(brew shellenv)")
endif

# libm for math.h (explicit; not always pulled in via notcurses.pc)
EXTRA_LDLIBS ?= -lm

# Prefer Clang when Make's implicit default is used (ignore implicit CC=cc).
ifeq ($(UNAME_S),Darwin)
  # Two words: must stay unquoted in the recipe so the shell runs xcrun(1) as the driver.
  DEFAULT_CC := xcrun clang
else
  DEFAULT_CC := clang
endif
ifeq ($(origin CC),default)
  CC := $(DEFAULT_CC)
endif

# If config.mk is missing, Make runs configure.sh then re-invokes itself (GNU make).
include config.mk

# Project CPPFLAGS (string macro must stay one shell word: '-DVERSION="2.0"').
CMATRIX_VER := 2.0

cmatrix: src/cmatrix.c src/util.c include/cmatrix.h include/matrix_rain_glyphs.h
	$(CC) $(STD) $(CFLAGS_OS) $(WARNFLAGS) '-DEXCLUDE_CONFIG_H' '-DVERSION="$(CMATRIX_VER)"' \
		$(CMATRIX_CPPFLAGS) $(CFLAGS) \
		$(PKG_NOTCURSES_CFLAGS) -I. -Iinclude \
		-o $@ src/cmatrix.c src/util.c \
		$(LDFLAGS) $(PKG_NOTCURSES_LIBS) $(EXTRA_LDLIBS)

all: cmatrix

config.mk: configure.sh
	CC="$(CC)" ./configure.sh

configure:
	@CC="$(CC)" ./configure.sh

clean:
	rm -f cmatrix src/cmatrix.o src/util.o

distclean: clean
	rm -f config.mk

install: cmatrix
	PREFIX="$(PREFIX)" DESTDIR="$(DESTDIR)" ./scripts/02_compile_install_bin.sh install

uninstall:
	PREFIX="$(PREFIX)" DESTDIR="$(DESTDIR)" ./scripts/02_compile_install_bin.sh uninstall
