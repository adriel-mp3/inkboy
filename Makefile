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
ASSET_HEADER  := src/duck_bitmap.h
ASSET_TOOL    := build/svg_to_bitmap
FBINK_DIR     := third_party/fbink
FBINK_VERSION := 886f25f
FBINK_SOURCES := fbink.c cutef8/utf8.c cutef8/dfa.c qimagescale/qimagescale.c
FBINK_OBJECTS := $(addprefix build/fbink/$(TARGET)/,$(FBINK_SOURCES:.c=.o))
HOST_CC       ?= gcc
AR            ?= ar
RANLIB        ?= ranlib

FBINK_CFLAGS  := -std=c11 -O2 -fno-semantic-interposition -ftree-vectorize \
	-funroll-loops -fno-common -fPIC -Wno-unused-function \
	-Wno-format -Wno-format-signedness -Wno-sign-conversion \
	-Wno-cast-align -Wno-conversion
FBINK_CPPFLAGS := -I$(FBINK_DIR) -D_REENTRANT=1 -D_GNU_SOURCE \
	-DFBINK_MINIMAL -DFBINK_WITH_IMAGE -DFBINK_WITH_DRAW \
	-DFBINK_VERSION=\"$(FBINK_VERSION)\"

.PHONY: all docker-image bootstrap docker-build host asset clean inspect

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

asset: $(ASSET_HEADER)

$(ASSET_TOOL): tools/svg_to_bitmap.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -Wall -Wextra -Wpedantic \
		$$(pkg-config --cflags cairo gobject-2.0) $< \
		$$(pkg-config --libs cairo gobject-2.0) -ldl -o $@

$(ASSET_HEADER): assets/duck.svg $(ASSET_TOOL)
	$(ASSET_TOOL) $< $@ 256 256

ifeq ($(TARGET),host)
CC           := gcc
OUTPUT       := build/$(PROJECT)-host
FBINK_PLATFORM := -DFBINK_FOR_LINUX
else
CC           := $(KINDLE_CC)
OUTPUT       := build/$(PROJECT)
FBINK_PLATFORM := -DFBINK_FOR_KINDLE
endif

build/fbink/$(TARGET)/%.o: $(FBINK_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(FBINK_CPPFLAGS) $(FBINK_PLATFORM) $(FBINK_CFLAGS) \
		-c $< -o $@

build/libfbink-$(TARGET).a: $(FBINK_OBJECTS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^
	$(RANLIB) $@

$(OUTPUT): $(SOURCE) $(ASSET_HEADER) build/libfbink-$(TARGET).a
	@mkdir -p $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) -D_POSIX_C_SOURCE=200809L -Isrc -I$(FBINK_DIR) $(SOURCE) \
		build/libfbink-$(TARGET).a -o $@

inspect: build/$(PROJECT)
	file build/$(PROJECT)
	readelf -l build/$(PROJECT) | grep 'Requesting program interpreter' || true
	readelf -d build/$(PROJECT) | grep 'NEEDED' || true

clean:
	rm -rf $(BUILD_DIR)
