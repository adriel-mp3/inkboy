PROJECT      := inkboy
IMAGE       ?= inkboy-build:0.1
ROOT_DIR     := $(abspath .)
BUILD_DIR    := $(ROOT_DIR)/build
TOOLCHAIN    ?= $(ROOT_DIR)/.toolchain
SDK_DIR      ?= $(ROOT_DIR)/.sdk
DOCKER_CONFIG ?= $(ROOT_DIR)/.docker
DOCKER_ENV    := DOCKER_CONFIG=$(DOCKER_CONFIG)
DOCKER        ?= docker

KINDLE_TC    ?= /opt/x-tools/arm-kindlehf-linux-gnueabihf
KINDLE_SYSROOT ?= $(KINDLE_TC)/arm-kindlehf-linux-gnueabihf/sysroot
KINDLE_CC   ?= $(KINDLE_TC)/bin/arm-kindlehf-linux-gnueabihf-gcc

COMMON_CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2
SOURCE        := src/main.c

.PHONY: all docker-image bootstrap docker-build host clean inspect

# The default is deliberately the isolated Kindle build. It will stop with a
# useful message until a Kindle SDK/sysroot has been bootstrapped.
all: docker-build

docker-image:
	mkdir -p $(DOCKER_CONFIG)
	$(DOCKER_ENV) $(DOCKER) build --tag $(IMAGE) --file docker/Dockerfile .

bootstrap: docker-image
	mkdir -p $(TOOLCHAIN) $(SDK_DIR)
	$(DOCKER_ENV) $(DOCKER) run --rm --privileged \
		--volume "$(TOOLCHAIN):/opt/x-tools" \
		--volume "$(SDK_DIR):/opt/kindle-sdk" \
		$(IMAGE) /usr/local/bin/bootstrap-kindlehf

docker-build: docker-image
	@test -x "$(TOOLCHAIN)/arm-kindlehf-linux-gnueabihf/bin/arm-kindlehf-linux-gnueabihf-gcc" || \
		(echo "Kindle toolchain not found. Run 'make bootstrap' first." >&2; exit 2)
	$(DOCKER_ENV) $(DOCKER) run --rm --user "$$(id -u):$$(id -g)" \
		--volume "$(ROOT_DIR):/workspace" \
		--volume "$(TOOLCHAIN):/opt/x-tools:ro" \
		--workdir /workspace \
		$(IMAGE) make TARGET=kindle KINDLE_TC=$(KINDLE_TC) \
		KINDLE_SYSROOT=$(KINDLE_SYSROOT) KINDLE_CC=$(KINDLE_CC) \
		build/$(PROJECT)

# A host build is only a source/UI smoke test. It does not establish Kindle
# ABI or runtime compatibility.
host: docker-image
	$(DOCKER_ENV) $(DOCKER) run --rm --user "$$(id -u):$$(id -g)" \
		--volume "$(ROOT_DIR):/workspace" --workdir /workspace \
		$(IMAGE) make TARGET=host build/$(PROJECT)-host

ifeq ($(TARGET),host)
CC           := gcc
PKG_ENV      := PKG_CONFIG_PATH= PKG_CONFIG_SYSROOT_DIR=
GTK_CFLAGS   := $(shell $(PKG_ENV) pkg-config --cflags gtk+-2.0 2>/dev/null)
GTK_LIBS     := $(shell $(PKG_ENV) pkg-config --libs gtk+-2.0 2>/dev/null)
OUTPUT       := $(BUILD_DIR)/$(PROJECT)-host
else
CC           := $(KINDLE_CC)
PKG_ENV      := PKG_CONFIG_PATH= PKG_CONFIG_LIBDIR=$(KINDLE_SYSROOT)/usr/lib/pkgconfig:$(KINDLE_SYSROOT)/usr/share/pkgconfig PKG_CONFIG_SYSROOT_DIR=$(KINDLE_SYSROOT)
GTK_CFLAGS   := $(shell $(PKG_ENV) pkg-config --cflags gtk+-2.0 2>/dev/null)
GTK_LIBS     := $(shell $(PKG_ENV) pkg-config --libs gtk+-2.0 2>/dev/null)
OUTPUT       := $(BUILD_DIR)/$(PROJECT)
endif

build/$(PROJECT): $(SOURCE)
	@mkdir -p $(BUILD_DIR)
	@test -n "$(GTK_CFLAGS)" -a -n "$(GTK_LIBS)" || \
		(echo "GTK2 was not found in the selected Kindle sysroot." >&2; exit 2)
	$(CC) $(COMMON_CFLAGS) $(GTK_CFLAGS) $< $(GTK_LIBS) -o $@

build/$(PROJECT)-host: $(SOURCE)
	@mkdir -p $(BUILD_DIR)
	@test -n "$(GTK_CFLAGS)" -a -n "$(GTK_LIBS)" || \
		(echo "GTK2 development files were not found in the build image." >&2; exit 2)
	$(CC) $(COMMON_CFLAGS) $(GTK_CFLAGS) $< $(GTK_LIBS) -o $@

inspect: build/$(PROJECT)
	file build/$(PROJECT)
	readelf -l build/$(PROJECT) | grep 'Requesting program interpreter' || true
	readelf -d build/$(PROJECT) | grep 'NEEDED' || true

clean:
	rm -rf $(BUILD_DIR)
