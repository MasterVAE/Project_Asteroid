.DEFAULT_GOAL := all

CMAKE ?= cmake
BUILD_DIR ?= build/release
CONFIG ?= Release
FETCH_DEPS ?= OFF
SANITIZERS ?= OFF
CMAKE_ARGS ?=
JOBS ?= 4
PREFIX ?= $(CURDIR)/dist/native

.PHONY: all release configure build run debug run-debug sanitize run-sanitize standalone install package clean clean-all windows windows-local help
all: build
release: build

configure:
	$(CMAKE) -S . -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE=$(CONFIG) -DASTEROID_FETCH_DEPENDENCIES=$(FETCH_DEPS) -DASTEROID_SANITIZERS=$(SANITIZERS) $(CMAKE_ARGS)

build: configure
	$(CMAKE) --build "$(BUILD_DIR)" --config $(CONFIG) --parallel $(JOBS)

run: build
	$(CMAKE) --build "$(BUILD_DIR)" --config $(CONFIG) --target run

debug:
	$(MAKE) build BUILD_DIR=build/debug CONFIG=Debug

run-debug:
	$(MAKE) run BUILD_DIR=build/debug CONFIG=Debug

sanitize:
	$(MAKE) build BUILD_DIR=build/sanitize CONFIG=Debug SANITIZERS=ON

run-sanitize:
	UBSAN_OPTIONS=print_stacktrace=1 $(MAKE) run BUILD_DIR=build/sanitize CONFIG=Debug SANITIZERS=ON

standalone:
	$(MAKE) build BUILD_DIR=build/standalone FETCH_DEPS=ON

install: build
	$(CMAKE) --install "$(BUILD_DIR)" --config $(CONFIG) --prefix "$(PREFIX)" --component Runtime

package: build
	$(CMAKE) --build "$(BUILD_DIR)" --config $(CONFIG) --target package

# Build Windows x64 using an installed MinGW-w64 toolchain.
windows:
	$(CMAKE) --preset windows-mingw $(CMAKE_ARGS)
	$(CMAKE) --build --preset windows-mingw --parallel $(JOBS)

windows-local: windows

clean:
	$(CMAKE) --build "$(BUILD_DIR)" --config $(CONFIG) --target clean

clean-all:
	$(CMAKE) -E rm -rf build

help:
	@echo "make [all|run|debug|run-debug|sanitize|run-sanitize|standalone]"
	@echo "make [install|package|clean] BUILD_DIR=build/release CONFIG=Release"
	@echo "make windows                     Windows x64 via installed MinGW"
	@echo "make windows-local               Alias for make windows"
	@echo "Options: JOBS=4 FETCH_DEPS=ON CMAKE_ARGS='...' PREFIX=..."
